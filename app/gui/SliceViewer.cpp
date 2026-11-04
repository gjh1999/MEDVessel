#include "SliceViewer.hpp"
#include "JobConfig.hpp"
#include "core/Crop.hpp"
#ifdef MEDVESSEL_GUI_NIFTI
#include "io/Nifti.hpp"
#endif
#ifdef MEDVESSEL_GUI_DICOM
#include "io/DicomSeries.hpp"
#endif
#include <QtWidgets>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QJsonArray>
#include <algorithm>
#include <limits>

namespace medvessel::desktop {
namespace {
constexpr int axes[3][3]={{0,1,2},{0,2,1},{1,2,0}};
using Volume=SliceViewer::Volume;
std::shared_ptr<Volume> readVolume(const QString& path) {
    if(QFileInfo(path).isDir()) {
#ifdef MEDVESSEL_GUI_DICOM
        return std::make_shared<Volume>(readDicomSeries(std::filesystem::u8path(path.toUtf8().toStdString())));
#else
        throw std::runtime_error("此构建未启用 DICOM 读取");
#endif
    }
#ifdef MEDVESSEL_GUI_NIFTI
    return std::make_shared<Volume>(readNifti(path.toUtf8().toStdString()));
#else
    throw std::runtime_error("此构建未启用 NIfTI 读取");
#endif
}
bool sameGrid(const Volume& a,const Volume& b) {
    if(a.dimensions()!=b.dimensions())return false;
    const auto& x=a.geometry();const auto& y=b.geometry();
    for(int i=0;i<3;++i)if(std::abs(x.spacing[i]-y.spacing[i])>1e-5||std::abs(x.origin[i]-y.origin[i])>1e-4)return false;
    for(int i=0;i<9;++i)if(std::abs(x.direction[i]-y.direction[i])>1e-6)return false;
    return true;
}
void validateMask(const Volume& mask) {
    for(float v:mask.data())if(!std::isfinite(v)||(v!=0&&v!=1))throw std::runtime_error("Mask 必须是有限的二值 0/1 数据");
}
QString directionName(const ImageGeometry& g,int axis,int sign) {
    QString name;const char* positive[]={"R","A","S"};const char* negative[]={"L","P","I"};
    for(int i=0;i<3;++i){double v=g.direction[i*3+axis]*sign;if(std::abs(v)>1e-4)name+=v>0?positive[i]:negative[i];}
    return name;
}
struct ReadResult {std::shared_ptr<Volume> volume,mask;QString path,error;};
}
SliceViewer::SliceViewer(QWidget* parent):QWidget(parent) {
    auto* layout=new QVBoxLayout(this);
    orientation_=new QComboBox;orientation_->setObjectName("sliceOrientation");orientation_->addItems({"原网格 · XY / XZ / YZ","标准解剖 · 轴位 / 冠状位 / 矢状位"});layout->addWidget(orientation_);
    orientation_->setToolTip("标准解剖按 RAS 方向显示，灰度线性插值、Mask 最近邻。种子仍写回原图索引；框选 ROI 请切回原网格。");
    connect(orientation_,&QComboBox::currentIndexChanged,this,[this]{dragStart_.reset();updateDisplay();refresh();});
    auto* controls=new QHBoxLayout;
    width_=new QDoubleSpinBox;width_->setObjectName("windowWidth");width_->setRange(1e-8,1e15);width_->setDecimals(6);width_->setValue(1);
    level_=new QDoubleSpinBox;level_->setObjectName("windowLevel");level_->setRange(-1e15,1e15);level_->setDecimals(6);
    controls->addWidget(new QLabel("窗宽"));controls->addWidget(width_);controls->addWidget(new QLabel("窗位"));controls->addWidget(level_);
    auto* range=new QPushButton("全范围");controls->addWidget(range);auto* ct=new QPushButton("CT 血管");controls->addWidget(ct);layout->addLayout(controls);
    connect(range,&QPushButton::clicked,this,&SliceViewer::fullRange);
    connect(ct,&QPushButton::clicked,this,[this]{width_->setValue(700);level_->setValue(250);});
    connect(width_,&QDoubleSpinBox::valueChanged,this,[this]{refresh();});connect(level_,&QDoubleSpinBox::valueChanged,this,[this]{refresh();});
    auto* overlay=new QHBoxLayout;auto* openMask=new QPushButton("载入 Mask…");showMask_=new QCheckBox("显示 Mask");showMask_->setObjectName("showMask");showMask_->setChecked(true);
    opacity_=new QSlider(Qt::Horizontal);opacity_->setObjectName("maskOpacity");opacity_->setRange(0,100);opacity_->setValue(40);auto* removeMask=new QPushButton("清除 Mask");
    overlay->addWidget(openMask);overlay->addWidget(showMask_);overlay->addWidget(opacity_);overlay->addWidget(removeMask);layout->addLayout(overlay);
    connect(openMask,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"选择二值 Mask",{},"NIfTI (*.nii *.nii.gz)");if(!path.isEmpty())loadMask(path);});
    connect(removeMask,&QPushButton::clicked,this,[this]{++maskGeneration_;mask_.reset();refresh();});
    connect(showMask_,&QCheckBox::toggled,this,[this]{refresh();});connect(opacity_,&QSlider::valueChanged,this,[this]{refresh();});
    auto* selection=new QHBoxLayout;mode_=new QComboBox;mode_->setObjectName("pickMode");mode_->addItems({"浏览","点选种子","框选 ROI","定位三维"});selection->addWidget(mode_);
    auto* seeds=new QPushButton("清除种子");seeds->setObjectName("clearSeeds");auto* roi=new QPushButton("清除 ROI");roi->setObjectName("clearRoi");selection->addWidget(seeds);selection->addWidget(roi);layout->addLayout(selection);
    if(auto* model=qobject_cast<QStandardItemModel*>(mode_->model()))model->item(3)->setEnabled(false);
    connect(seeds,&QPushButton::clicked,this,[this]{if(picking_&&!readOnly_)emit clearSeedsRequested();});connect(roi,&QPushButton::clicked,this,[this]{if(picking_&&!readOnly_)emit clearRoiRequested();});
    for(int p=0;p<3;++p){labels_[p]=new QLabel;labels_[p]->setWordWrap(true);layout->addWidget(labels_[p]);scenes_[p]=new QGraphicsScene(this);views_[p]=new QGraphicsView(scenes_[p]);views_[p]->setObjectName(QString("sliceView%1").arg(p));views_[p]->setMinimumHeight(100);views_[p]->setBackgroundBrush(Qt::black);views_[p]->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);views_[p]->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);views_[p]->viewport()->installEventFilter(this);layout->addWidget(views_[p],1);
        sliders_[p]=new QSlider(Qt::Horizontal);sliders_[p]->setObjectName(QString("slice%1").arg(p));layout->addWidget(sliders_[p]);connect(sliders_[p],&QSlider::valueChanged,this,[this]{dragStart_.reset();refresh();});}
    status_=new QLabel("选择影像后载入切片");status_->setWordWrap(true);layout->addWidget(status_);
}
void SliceViewer::clear(){++generation_;++maskGeneration_;volume_.reset();mask_.reset();display_={};focus_.reset();dragStart_.reset();imageKeys_={};images_={};setProperty("volumePath",QString());refresh();}
void SliceViewer::setVolume(std::shared_ptr<Volume> volume,const QString& path){
    ++generation_;++maskGeneration_;volume_=std::move(volume);mask_.reset();dragStart_.reset();setProperty("volumePath",path);
    status_->setText(path);display_={};updateDisplay();
    fullRange();if(focus_){auto target=*focus_;focus_.reset();QString error;if(!focusWorld(target,&error))status_->setText(error);}refresh();
}
void SliceViewer::updateDisplay(){
    if(!volume_)return;
    std::optional<Point3D> target=focus_;
    if(!target&&display_.dimensions[0])target=display_.geometry.indexToWorld({double(sliders_[2]->value()),double(sliders_[1]->value()),double(sliders_[0]->value())});
    try{
        display_=orientation_->currentIndex()==1?anatomicalGrid(*volume_):DisplayGrid{volume_->geometry(),volume_->dimensions()};
        for(int p=0;p<3;++p)if(display_.dimensions[axes[p][0]]>16777216/display_.dimensions[axes[p][1]])throw std::runtime_error("标准平面过大，请使用原网格浏览");
    }catch(const std::exception& e){QSignalBlocker block(orientation_);orientation_->setCurrentIndex(0);display_={volume_->geometry(),volume_->dimensions()};status_->setText(QString::fromUtf8(e.what()));}
    auto index=target?display_.geometry.worldToIndex(*target):Point3D{};
    for(int p=0;p<3;++p){QSignalBlocker block(sliders_[p]);int axis=axes[p][2];sliders_[p]->setRange(0,int(display_.dimensions[axis])-1);sliders_[p]->setValue(target?int(std::clamp(std::round(index[axis]),0.,double(sliders_[p]->maximum()))):sliders_[p]->maximum()/2);}
    if(auto* model=qobject_cast<QStandardItemModel*>(mode_->model()))model->item(2)->setEnabled(!readOnly_&&orientation_->currentIndex()==0);
    if(orientation_->currentIndex()==1&&mode_->currentIndex()==2)mode_->setCurrentIndex(0);
}
void SliceViewer::fullRange(){
    if(!volume_)return;
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(float v:volume_->data())if(std::isfinite(v)){low=std::min(low,double(v));high=std::max(high,double(v));}
    if(!std::isfinite(low)){low=0;high=0;}
    {QSignalBlocker a(width_),b(level_);width_->setValue(high>low?high-low:1);level_->setValue(high>low?(low+high)/2:low+.5);}refresh();
}
bool SliceViewer::setMask(std::shared_ptr<Volume> mask,const QString& path,QString* error){
    ++maskGeneration_;mask_.reset();
    try{if(!mask||!volume_||!sameGrid(*volume_,*mask))throw std::runtime_error("Mask 与底图网格不一致");validateMask(*mask);mask_=std::move(mask);status_->setText(path);refresh();return true;}
    catch(const std::exception& e){if(error)*error=QString::fromUtf8(e.what());status_->setText(QString::fromUtf8(e.what()));refresh();return false;}
}
QImage SliceViewer::sliceImage(int plane,int index)const{
    if(!volume_||plane<0||plane>2)return {};
    auto d=display_.dimensions;auto a=axes[plane];if(index<0||index>=int(d[a[2]]))return {};
    ImageKey key{generation_,maskGeneration_,orientation_->currentIndex(),index,width_->value(),level_->value(),opacity_->value(),showMask_->isChecked()};
    if(imageKeys_[plane]&&*imageKeys_[plane]==key)return images_[plane];
    QImage image(int(d[a[0]]),int(d[a[1]]),QImage::Format_RGB32);double width=width_->value(),low=level_->value()-width/2,alpha=opacity_->value()/100.;
    if(image.isNull())return {};
    // Compute the display-to-source affine once, rather than validating both
    // direction matrices for every pixel. No resampled volume is retained.
    auto base=volume_->geometry().worldToIndex(display_.geometry.origin);std::array<Point3D,3> delta{};
    for(int j=0;j<3;++j){Point3D unit{};unit[j]=1;auto v=volume_->geometry().worldToIndex(display_.geometry.indexToWorld(unit));for(int i=0;i<3;++i)delta[j][i]=v[i]-base[i];}
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){std::array<int,3> q{};q[a[0]]=x;q[a[1]]=image.height()-1-y;q[a[2]]=index;
        std::optional<std::array<std::size_t,3>> nearest;double value;
        if(orientation_->currentIndex()==0){nearest=std::array<std::size_t,3>{std::size_t(q[0]),std::size_t(q[1]),std::size_t(q[2])};value=volume_->at(q[0],q[1],q[2]);}
        else{Point3D sourceIndex=base;for(int i=0;i<3;++i)for(int j=0;j<3;++j)sourceIndex[i]+=q[j]*delta[j][i];nearest=nearestSampleIndex(sourceIndex,volume_->dimensions());value=linearSample(*volume_,sourceIndex);}
        int gray=std::isfinite(value)?int(std::clamp((value-low)/width*255.,0.,255.)):0;QColor color(gray,gray,gray);
        if(mask_&&showMask_->isChecked()&&nearest&&mask_->at((*nearest)[0],(*nearest)[1],(*nearest)[2])==1)color=QColor(int(gray*(1-alpha)+255*alpha),int(gray*(1-alpha)+128*alpha),int(gray*(1-alpha)));
        image.setPixelColor(x,y,color);}
    images_[plane]=image;imageKeys_[plane]=key;
    return image;
}
void SliceViewer::refresh(){
    for(int p=0;p<3;++p){scenes_[p]->clear();if(!volume_){labels_[p]->clear();continue;}auto image=sliceImage(p,sliders_[p]->value());auto* item=scenes_[p]->addPixmap(QPixmap::fromImage(image));const auto& g=display_.geometry;auto a=axes[p];item->setTransform(QTransform::fromScale(g.spacing[a[0]],g.spacing[a[1]]));
        QPen seedPen(Qt::cyan);seedPen.setCosmetic(true);QPen roiPen(Qt::green);roiPen.setCosmetic(true);QPen focusPen(Qt::yellow);focusPen.setCosmetic(true);focusPen.setStyle(Qt::DashLine);
        for(const auto& seed:seeds_.split(';',Qt::SkipEmptyParts)){auto parts=seed.split(',');if(parts.size()!=3)continue;Point3D q{};bool valid=true;for(int i=0;i<3;++i){bool ok;q[i]=parts[i].trimmed().toInt(&ok);valid&=ok&&q[i]>=0&&q[i]<double(volume_->dimensions()[i]);}if(!valid)continue;q=g.worldToIndex(volume_->geometry().indexToWorld(q));if(std::round(q[a[2]])==sliders_[p]->value()){auto* mark=new QGraphicsEllipseItem(q[a[0]]+.15,image.height()-q[a[1]]-.85,.7,.7,item);mark->setPen(seedPen);}}
        auto box=roi_.split(',');if(orientation_->currentIndex()==0&&box.size()==6){std::array<int,6> r{};bool valid=true;for(int i=0;i<6;++i){bool ok;r[i]=box[i].trimmed().toInt(&ok);valid&=ok;}for(int i=0;i<3;++i)valid&=r[i]>=0&&r[i+3]>0&&double(r[i])+r[i+3]<=double(volume_->dimensions()[i]);if(valid&&sliders_[p]->value()>=r[a[2]]&&sliders_[p]->value()<r[a[2]]+r[a[2]+3]){auto* outline=new QGraphicsRectItem(r[a[0]],image.height()-r[a[1]]-r[a[1]+3],r[a[0]+3],r[a[1]+3],item);outline->setPen(roiPen);}}
        if(focus_){auto q=g.worldToIndex(*focus_);double x=std::round(q[a[0]])+.5,y=image.height()-std::round(q[a[1]])-.5;auto* h=new QGraphicsLineItem(0,y,image.width(),y,item);h->setPen(focusPen);auto* v=new QGraphicsLineItem(x,0,x,image.height(),item);v->setPen(focusPen);}
        scenes_[p]->setSceneRect(item->mapRectToScene(item->boundingRect()));views_[p]->fitInView(scenes_[p]->sceneRect(),Qt::KeepAspectRatio);
        Point3D center{};for(int i=0;i<3;++i)center[i]=(display_.dimensions[i]-1)/2.;center[a[2]]=sliders_[p]->value();auto world=g.indexToWorld(center);
        labels_[p]->setText(QString("%1 · %2/%3  左 %4 / 右 %5 / 上 %6 / 下 %7  RAS (%8, %9, %10) mm").arg(orientation_->currentIndex()==0?QStringList{"XY","XZ","YZ"}[p]:QStringList{"轴位","冠状位","矢状位"}[p]).arg(sliders_[p]->value()).arg(sliders_[p]->maximum()).arg(directionName(g,a[0],-1),directionName(g,a[0],1),directionName(g,a[1],1),directionName(g,a[1],-1)).arg(world[0],0,'f',2).arg(world[1],0,'f',2).arg(world[2],0,'f',2));}
}
void SliceViewer::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);refresh();}
void SliceViewer::setReadOnly(){
    readOnly_=true;mode_->setCurrentIndex(0);
    if(auto* model=qobject_cast<QStandardItemModel*>(mode_->model())){model->item(1)->setEnabled(false);model->item(2)->setEnabled(false);model->item(3)->setEnabled(true);}
    findChild<QPushButton*>("clearSeeds")->hide();findChild<QPushButton*>("clearRoi")->hide();
}
void SliceViewer::setPickingEnabled(bool enabled){picking_=enabled;dragStart_.reset();}
void SliceViewer::setSelection(const QString& roi,const QString& seeds){roi_=roi;seeds_=seeds;refresh();}
bool SliceViewer::focusWorld(const Point3D& world,QString* error){
    auto fail=[&]{if(error)*error="所选位置不在当前结果切片网格内";return false;};for(double v:world)if(!std::isfinite(v))return fail();
    if(volume_){auto q=volume_->geometry().worldToIndex(world);for(int i=0;i<3;++i)if(q[i]<-1e-3||q[i]>double(volume_->dimensions()[i]-1)+1e-3)return fail();
        q=display_.geometry.worldToIndex(world);
        for(int p=0;p<3;++p){QSignalBlocker b(sliders_[p]);sliders_[p]->setValue(int(std::round(q[axes[p][2]])));}}
    focus_=world;refresh();return true;
}
void SliceViewer::clearFocus(){focus_.reset();refresh();}
std::optional<std::array<int,3>> SliceViewer::voxelAt(int plane,const QPoint& point,bool clamp)const{
    if(!volume_)return {};
    auto a=axes[plane];auto d=display_.dimensions;auto p=views_[plane]->mapToScene(point);const auto& s=display_.geometry.spacing;
    int x=int(std::floor(p.x()/s[a[0]])),y=int(d[a[1]])-1-int(std::floor(p.y()/s[a[1]]));
    if(!clamp&&(x<0||y<0||x>=int(d[a[0]])||y>=int(d[a[1]])))return {};
    std::array<int,3> q{};q[a[0]]=std::clamp(x,0,int(d[a[0]])-1);q[a[1]]=std::clamp(y,0,int(d[a[1]])-1);q[a[2]]=sliders_[plane]->value();return q;
}
std::optional<std::array<int,3>> SliceViewer::sourceVoxel(const std::array<int,3>& index)const{
    if(!volume_)return {};
    auto world=display_.geometry.indexToWorld({double(index[0]),double(index[1]),double(index[2])});
    auto nearest=nearestSampleIndex(volume_->geometry().worldToIndex(world),volume_->dimensions());
    if(!nearest)return {};
    return std::array<int,3>{int((*nearest)[0]),int((*nearest)[1]),int((*nearest)[2])};
}
bool SliceViewer::eventFilter(QObject* object,QEvent* event){
    for(int p=0;p<3;++p)if(object==views_[p]->viewport()){
        if(event->type()!=QEvent::MouseButtonPress&&event->type()!=QEvent::MouseButtonRelease)return QWidget::eventFilter(object,event);
        auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton||!volume_)break;int mode=mode_->currentIndex();
        if(mode==3){if(event->type()==QEvent::MouseButtonRelease){auto q=voxelAt(p,mouse->position().toPoint(),false);if(q&&sourceVoxel(*q)){auto w=display_.geometry.indexToWorld({double((*q)[0]),double((*q)[1]),double((*q)[2])});if(focusWorld(w))emit worldPicked(w[0],w[1],w[2]);}}return true;}
        if(!picking_||readOnly_)break;
        if(mode==1&&event->type()==QEvent::MouseButtonRelease){auto q=voxelAt(p,mouse->position().toPoint(),false);auto source=q?sourceVoxel(*q):std::nullopt;if(source)emit seedPicked(QString("%1,%2,%3").arg((*source)[0]).arg((*source)[1]).arg((*source)[2]));return true;}
        if(mode==2&&orientation_->currentIndex()==0){if(event->type()==QEvent::MouseButtonPress){dragStart_=voxelAt(p,mouse->position().toPoint(),false);dragPlane_=p;}
            else if(dragStart_&&dragPlane_==p){auto end=voxelAt(p,mouse->position().toPoint(),true);if(end){std::array<int,6> r{0,0,0,int(volume_->dimensions()[0]),int(volume_->dimensions()[1]),int(volume_->dimensions()[2])};auto parts=roi_.split(',');if(parts.size()==6){auto old=r;bool valid=true;for(int i=0;i<6;++i){bool ok;old[i]=parts[i].trimmed().toInt(&ok);valid&=ok;}for(int i=0;i<3;++i)valid&=old[i]>=0&&old[i+3]>0&&double(old[i])+old[i+3]<=double(volume_->dimensions()[i]);if(valid)r=old;}
                for(int i=0;i<2;++i){int axis=axes[p][i];r[axis]=std::min((*dragStart_)[axis],(*end)[axis]);r[axis+3]=std::abs((*dragStart_)[axis]-(*end)[axis])+1;}QStringList values;for(int v:r)values<<QString::number(v);emit roiPicked(values.join(','));}dragStart_.reset();}return true;}
    }return QWidget::eventFilter(object,event);
}
void SliceViewer::load(const QString& path){request(path,false);}
void SliceViewer::loadResult(const QString& path){request(path,true);}
void SliceViewer::request(const QString& path,bool result){
    auto target=focus_;clear();focus_=target;const auto token=generation_;status_->setText("正在读取影像…");auto* watcher=new QFutureWatcher<ReadResult>(this);
    connect(watcher,&QFutureWatcher<ReadResult>::finished,this,[this,watcher,token]{auto r=watcher->result();watcher->deleteLater();if(token!=generation_)return;
        if(!r.error.isEmpty()){status_->setText(r.error);emit loaded(false);return;}setVolume(r.volume,r.path);if(r.mask)setMask(r.mask,r.path);emit loaded(true);});
    watcher->setFuture(QtConcurrent::run([path,result]{ReadResult r;r.path=path;try{if(!result)r.volume=readVolume(path);else{
        auto record=readJson(QDir(path).filePath("run.json"));r.mask=readVolume(QDir(path).filePath("mask.nii.gz"));validateMask(*r.mask);
        auto dims=record["dimensions"].toArray(),spacing=record["spacing_mm"].toArray();if(dims.size()!=3||spacing.size()!=3)throw std::runtime_error("结果缺少合法网格信息");
        for(int i=0;i<3;++i)if(dims[i].toDouble()!=double(r.mask->dimensions()[i])||!std::isfinite(spacing[i].toDouble())||std::abs(spacing[i].toDouble()-r.mask->geometry().spacing[i])>1e-5)throw std::runtime_error("结果 Mask 与运行记录网格不一致");
        try{auto input=record["input_path"].toString();if(input.isEmpty())throw std::runtime_error("没有原图路径");r.volume=readVolume(input);auto roi=record["roi_voxel"].toArray();if(!roi.isEmpty()){if(roi.size()!=6)throw std::runtime_error("无效 ROI");std::array<std::size_t,3> start{},size{};for(int i=0;i<3;++i){double a=roi[i].toDouble(-1),b=roi[i+3].toDouble(-1);if(a<0||b<=0||std::floor(a)!=a||std::floor(b)!=b||a+b>double(r.volume->dimensions()[i]))throw std::runtime_error("无效 ROI");start[i]=std::size_t(a);size[i]=std::size_t(b);}r.volume=std::make_shared<Volume>(cropVolume(*r.volume,start,size));}if(!sameGrid(*r.volume,*r.mask))throw std::runtime_error("原图网格不符");}
        catch(const std::exception& e){r.volume=r.mask;r.path="仅显示结果 Mask（无原始灰度）："+QString::fromUtf8(e.what());}
    }}catch(const std::exception& e){r.error=QString::fromUtf8(e.what());}return r;}));
}
void SliceViewer::loadMask(const QString& path){
    mask_.reset();refresh();const auto token=++maskGeneration_,base=generation_;auto* watcher=new QFutureWatcher<ReadResult>(this);
    connect(watcher,&QFutureWatcher<ReadResult>::finished,this,[this,watcher,token,base]{auto r=watcher->result();watcher->deleteLater();if(token!=maskGeneration_||base!=generation_)return;bool ok=false;if(r.error.isEmpty())ok=setMask(r.volume,r.path);else status_->setText(r.error);emit maskLoaded(ok);});
    watcher->setFuture(QtConcurrent::run([path]{ReadResult r;r.path=path;try{r.volume=readVolume(path);}catch(const std::exception& e){r.error=QString::fromUtf8(e.what());}return r;}));
}
}

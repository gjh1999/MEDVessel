#include "SliceViewer.hpp"
#include "JobConfig.hpp"
#include "core/Crop.hpp"
#include <QJsonArray>
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
#ifdef MEDVESSEL_GUI_NIFTI
#include "io/Nifti.hpp"
#endif
#ifdef MEDVESSEL_GUI_DICOM
#include "io/DicomSeries.hpp"
#endif
#include <algorithm>
#include <limits>
namespace medvessel::desktop {
namespace {
// Native image planes: horizontal, vertical, slice axis. Screen vertical is reversed.
constexpr int axes[3][3]={{0,1,2},{0,2,1},{1,2,0}};
QString directionName(const ImageGeometry& g,int axis,int sign) {
    QString result;
    const char* positive[]={"R","A","S"}; const char* negative[]={"L","P","I"};
    std::array<std::pair<double,int>,3> components;
    for(int i=0;i<3;++i)components[i]={std::abs(g.direction[i*3+axis]),i};
    std::sort(components.rbegin(),components.rend());
    for(auto [m,i]:components)if(m>0.01)result+=g.direction[i*3+axis]*sign>0?positive[i]:negative[i];
    return result;
}
}
SliceViewer::SliceViewer(QWidget* parent):QWidget(parent) {
    auto* root=new QVBoxLayout(this);
    status_=new QLabel("点击“浏览输入切片”载入当前输入影像。");status_->setWordWrap(true);status_->setObjectName("sliceStatus");root->addWidget(status_);
    auto* windowRow=new QHBoxLayout;
    width_=new QDoubleSpinBox;width_->setObjectName("windowWidth");width_->setRange(0.000001,1e30);width_->setDecimals(6);width_->setValue(1);
    level_=new QDoubleSpinBox;level_->setObjectName("windowLevel");level_->setRange(-1e30,1e30);level_->setDecimals(6);
    width_->setFixedWidth(115);level_->setFixedWidth(115);width_->setKeyboardTracking(false);level_->setKeyboardTracking(false);
    auto* reset=new QPushButton("全范围");auto* preset=new QPushButton("CT 血管");
    windowRow->addWidget(new QLabel("窗宽"));windowRow->addWidget(width_);windowRow->addWidget(new QLabel("窗位"));windowRow->addWidget(level_);windowRow->addWidget(reset);windowRow->addWidget(preset);root->addLayout(windowRow);
    connect(width_,&QDoubleSpinBox::valueChanged,this,[this]{refresh();});connect(level_,&QDoubleSpinBox::valueChanged,this,[this]{refresh();});
    connect(reset,&QPushButton::clicked,this,[this]{QSignalBlocker b(width_);width_->setValue(maximum_-minimum_);level_->setValue((minimum_+maximum_)/2);refresh();});
    preset->setToolTip("窗宽 700、窗位 250；仅在输入灰度为 HU 时具有 CT 含义");
    connect(preset,&QPushButton::clicked,this,[this]{QSignalBlocker b(width_);width_->setValue(700);level_->setValue(250);refresh();});
    auto* maskRow=new QHBoxLayout;maskButton_=new QPushButton("载入 Mask…");maskButton_->setEnabled(false);maskRow->addWidget(maskButton_);
    overlay_=new QCheckBox("显示 Mask");overlay_->setObjectName("showMask");overlay_->setChecked(true);maskRow->addWidget(overlay_);
    opacity_=new QSlider(Qt::Horizontal);opacity_->setObjectName("maskOpacity");opacity_->setRange(0,100);opacity_->setValue(40);maskRow->addWidget(new QLabel("透明度"));maskRow->addWidget(opacity_);
    auto* clear=new QPushButton("清除");maskRow->addWidget(clear);root->addLayout(maskRow);
    maskStatus_=new QLabel("未载入 Mask（橙色）");maskStatus_->setWordWrap(true);maskStatus_->setObjectName("maskStatus");root->addWidget(maskStatus_);
    connect(overlay_,&QCheckBox::toggled,this,[this]{refresh();});connect(opacity_,&QSlider::valueChanged,this,[this]{refresh();});
    connect(clear,&QPushButton::clicked,this,[this]{if(maskWatcher_.isRunning())return;mask_.reset();maskStatus_->setText("未载入 Mask（橙色）");refresh();});
    connect(maskButton_,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,"选择与当前影像同网格的 Mask",{},"NIfTI (*.nii *.nii.gz)");if(!p.isEmpty())loadMask(p);});
    pickControls_=new QWidget;auto* pickRow=new QHBoxLayout(pickControls_);pickRow->setContentsMargins(0,0,0,0);pickMode_=new QComboBox;pickMode_->setObjectName("pickMode");pickMode_->addItems({"浏览","点选种子","框选 ROI"});pickRow->addWidget(pickMode_);
    auto* clearSeeds=new QPushButton("清除种子");auto* clearRoi=new QPushButton("清除 ROI");pickRow->addWidget(clearSeeds);pickRow->addWidget(clearRoi);root->addWidget(pickControls_);
    connect(clearSeeds,&QPushButton::clicked,this,&SliceViewer::clearSeedsRequested);connect(clearRoi,&QPushButton::clicked,this,&SliceViewer::clearRoiRequested);
    selectionHint_=new QLabel("点选追加原图种子；框选修改当前平面两轴，另一轴沿用 ROI 或全范围，可在另一视图继续收紧。");selectionHint_->setWordWrap(true);root->addWidget(selectionHint_);
    locateThreeD_=new QPushButton("查看三维定位");locateThreeD_->setObjectName("locateThreeD");locateThreeD_->setEnabled(false);locateThreeD_->hide();root->addWidget(locateThreeD_);
    connect(locateThreeD_,&QPushButton::clicked,this,&SliceViewer::locateThreeDRequested);
    focusStatus_=new QLabel;focusStatus_->setWordWrap(true);focusStatus_->setObjectName("focusStatus");root->addWidget(focusStatus_);
    connect(pickMode_,&QComboBox::currentIndexChanged,this,[this]{dragPlane_=-1;if(rubber_)rubber_->hide();});
    auto* panelScroll=new QScrollArea;panelScroll->setWidgetResizable(true);panelScroll->setFrameShape(QFrame::NoFrame);
    auto* panelBody=new QWidget;auto* panels=new QVBoxLayout(panelBody);panels->setContentsMargins(0,0,0,0);panelScroll->setWidget(panelBody);root->addWidget(panelScroll,1);
    for(int p=0;p<3;++p) {
        auto* box=new QWidget;auto* layout=new QVBoxLayout(box);layout->setContentsMargins(0,0,0,0);
        labels_[p]=new QLabel;layout->addWidget(labels_[p]);
        scenes_[p]=new QGraphicsScene(this);views_[p]=new QGraphicsView(scenes_[p]);
        views_[p]->setObjectName(QString("sliceView%1").arg(p));
        views_[p]->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);views_[p]->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        views_[p]->viewport()->installEventFilter(this);
        views_[p]->setBackgroundBrush(Qt::black);views_[p]->setMinimumHeight(80);layout->addWidget(views_[p],1);
        sliders_[p]=new QSlider(Qt::Horizontal);sliders_[p]->setObjectName(QString("slice%1").arg(p));sliders_[p]->setEnabled(false);layout->addWidget(sliders_[p]);
        connect(sliders_[p],&QSlider::valueChanged,this,[this,p]{dragPlane_=-1;if(rubber_)rubber_->hide();render(p);});panels->addWidget(box,1);
    }
    connect(&watcher_,&QFutureWatcher<LoadResult>::finished,this,[this]{
        if(!pendingResult_.isEmpty()){auto next=pendingResult_;pendingResult_.clear();loadResult(next);return;}
        if(watcher_.property("generation").toULongLong()!=generation_)return;
        auto result=watcher_.result();
        if(result.volume){setVolume(result.volume,result.name.isEmpty()?property("loadingPath").toString():result.name);if(result.mask){QString error;if(!setMask(result.mask,"当前分析结果",&error)){status_->setText(error);emit loaded(false);return;}}emit loaded(true);}
        else {status_->setText("载入失败："+result.error);emit loaded(false);}
    });
    connect(&maskWatcher_,&QFutureWatcher<LoadResult>::finished,this,[this]{if(maskWatcher_.property("generation").toULongLong()!=generation_)return;auto r=maskWatcher_.result();QString error=r.error;bool ok=r.volume&&setMask(r.volume,property("maskPath").toString(),&error);if(!ok)maskStatus_->setText("Mask 载入失败："+error);maskButton_->setEnabled(bool(volume_));emit maskLoaded(ok);});
}
void SliceViewer::setReadOnly(){resultPicking_=true;locateThreeD_->show();setPickingEnabled(false);pickControls_->hide();selectionHint_->hide();maskButton_->hide();status_->setText("在三维选择分支后定位到此页；也可单击结果切片，再点击“查看三维定位”。");}
void SliceViewer::clearFocus(){focusWorld_.reset();focusVoxel_.reset();focusStatus_->clear();locateThreeD_->setEnabled(false);refresh();}
void SliceViewer::clear(){locateThreeD_->setEnabled(false);++generation_;pendingResult_.clear();focusWorld_.reset();focusVoxel_.reset();focusStatus_->clear();volume_.reset();mask_.reset();setProperty("volumePath",QString());status_->setText("尚未载入结果切片");maskStatus_->setText("未载入 Mask");maskButton_->setEnabled(false);dragPlane_=-1;if(rubber_)rubber_->hide();for(int p=0;p<3;++p){scenes_[p]->clear();labels_[p]->clear();sliders_[p]->setEnabled(false);}}
bool SliceViewer::focusWorld(const Point3D& world,QString* error){
    auto fail=[&](const QString& text){locateThreeD_->setEnabled(false);focusVoxel_.reset();focusStatus_->setText(text);if(error)*error=text;refresh();return false;};
    for(double v:world)if(!std::isfinite(v))return fail("定位坐标必须有限");
    focusWorld_=world;if(!volume_){focusStatus_->setText("等待结果切片载入后定位…");return true;}
    auto index=volume_->geometry().worldToIndex(world);auto d=volume_->dimensions();std::array<std::size_t,3> q{};
    for(int a=0;a<3;++a){if(index[a]<-1e-3||index[a]>double(d[a]-1)+1e-3)return fail("所选位置不在当前结果切片网格内");q[a]=static_cast<std::size_t>(std::clamp(std::round(index[a]),0.,double(d[a]-1)));}
    focusVoxel_=q;locateThreeD_->setEnabled(resultPicking_);dragPlane_=-1;if(rubber_)rubber_->hide();for(int p=0;p<3;++p){QSignalBlocker blocker(sliders_[p]);sliders_[p]->setValue(int(q[axes[p][2]]));}
    focusStatus_->setText(QString("黄色十字 · 结果网格索引 %1,%2,%3 · RAS mm %4,%5,%6").arg(q[0]).arg(q[1]).arg(q[2]).arg(world[0],0,'f',2).arg(world[1],0,'f',2).arg(world[2],0,'f',2));refresh();return true;
}
void SliceViewer::loadResult(const QString& directory){
    if(watcher_.isRunning()){++generation_;pendingResult_=directory;return;}
    auto focus=focusWorld_;clear();focusWorld_=focus;status_->setText("正在读取结果底图与 Mask…");watcher_.setProperty("generation",QVariant::fromValue(generation_));
    watcher_.setFuture(QtConcurrent::run([directory]{LoadResult result;try {
#ifdef MEDVESSEL_GUI_NIFTI
        QDir dir(directory);auto run=readJson(dir.filePath("run.json"));auto mask=std::make_shared<Volume3D<float>>(readNifti(dir.filePath("mask.nii.gz").toUtf8().toStdString()));
        const auto dims=run["dimensions"].toArray(),spacing=run["spacing_mm"].toArray();if(run["schema_version"].toInt()!=1||dims.size()!=3||spacing.size()!=3)throw std::runtime_error("运行记录缺少结果网格信息");
        for(int a=0;a<3;++a)if(mask->dimensions()[a]>static_cast<std::size_t>(std::numeric_limits<int>::max())||dims[a].toDouble(-1)!=double(mask->dimensions()[a])||std::abs(spacing[a].toDouble(-1)-mask->geometry().spacing[a])>1e-5)throw std::runtime_error("Mask 网格与运行记录不一致");
        for(float v:mask->data())if(!std::isfinite(v)||(v!=0&&v!=1))throw std::runtime_error("结果 Mask 必须为有限的 0/1 数据");
        const auto path=run["input_path"].toString();result.mask=mask;
        try {
            if(path.isEmpty()||!QFileInfo::exists(path))throw std::runtime_error("原始输入路径不可用");
            auto source=[&](){if(QFileInfo(path).isDir()){
#ifdef MEDVESSEL_GUI_DICOM
                return readDicomSeries(std::filesystem::u8path(path.toUtf8().toStdString()));
#else
                throw std::runtime_error("当前构建不支持 DICOM");
#endif
            }return readNifti(path.toUtf8().toStdString());}();
            if(!run["roi_voxel"].isNull()&&!run["roi_voxel"].isUndefined()){auto roi=run["roi_voxel"].toArray();if(roi.size()!=6)throw std::runtime_error("结果 ROI 格式错误");std::array<std::size_t,6> r{};auto dims=source.dimensions();for(int a=0;a<6;++a){double v=roi[a].toDouble(-1);if(!std::isfinite(v)||v<0||v!=std::floor(v)||v>double(dims[a%3]))throw std::runtime_error("结果 ROI 索引无效");r[a]=static_cast<std::size_t>(v);}source=cropVolume(source,{r[0],r[1],r[2]},{r[3],r[4],r[5]});}
            const auto& a=source.geometry();const auto& b=mask->geometry();bool same=source.dimensions()==mask->dimensions();for(int i=0;i<3;++i)same= same&&std::abs(a.spacing[i]-b.spacing[i])<=1e-5&&std::abs(a.origin[i]-b.origin[i])<=1e-4;for(int i=0;i<9;++i)same=same&&std::abs(a.direction[i]-b.direction[i])<=1e-6;if(!same)throw std::runtime_error("原图与结果网格不一致");
            result.volume=std::make_shared<Volume3D<float>>(std::move(source));result.name="结果底图（按运行记录裁剪）："+path;
        }catch(const std::exception& e){result.volume=mask;result.name="仅显示结果 Mask（无原始灰度）："+QString::fromUtf8(e.what());}
#else
        throw std::runtime_error("当前构建不支持 NIfTI");
#endif
    }catch(const std::exception& e){result.volume.reset();result.error=QString::fromUtf8(e.what());}return result;}));
}
void SliceViewer::setPickingEnabled(bool enabled) {
    pickingEnabled_=enabled;pickMode_->setEnabled(enabled);
    selectionHint_->setText(enabled?"点选追加原图种子；框选修改当前平面两轴，另一轴沿用 ROI 或全范围，可在另一视图继续收紧。":"选取已禁用：请勾选左侧“种子使用原图索引”，并等待分析结束。已有种子不会自动转换坐标。");
    if(!enabled){pickMode_->setCurrentIndex(0);dragPlane_=-1;if(rubber_)rubber_->hide();}
}
void SliceViewer::setSelection(const QString& roi,const QString& seeds) {
    roiText_=roi;seedsText_=seeds;roi_.reset();seeds_.clear();
    if(!volume_)return;
    const auto d=volume_->dimensions();
    auto parse=[](const QString& s,int count,std::vector<std::size_t>& out){auto parts=s.split(',');if(parts.size()!=count)return false;for(auto part:parts){bool ok=false;auto v=part.trimmed().toULongLong(&ok);if(!ok||part.trimmed().startsWith('-'))return false;out.push_back(static_cast<std::size_t>(v));}return true;};
    std::vector<std::size_t> values;
    if(parse(roi,6,values)){bool valid=true;for(int a=0;a<3;++a)if(values[a]>=d[a]||values[a+3]==0||values[a+3]>d[a]-values[a])valid=false;if(valid){std::array<std::size_t,6> r{};std::copy(values.begin(),values.end(),r.begin());roi_=r;}}
    for(auto point:seeds.split(';',Qt::SkipEmptyParts)){values.clear();if(parse(point,3,values)&&values[0]<d[0]&&values[1]<d[1]&&values[2]<d[2])seeds_.push_back({values[0],values[1],values[2]});}
    refresh();
}
bool SliceViewer::voxelAt(int p,const QPoint& viewport,std::array<std::size_t,3>& q,bool clamp) const {
    if(!volume_)return false;
    auto d=volume_->dimensions();const auto* a=axes[p];auto spacing=volume_->geometry().spacing;
    auto point=views_[p]->mapToScene(viewport);double x=point.x()/spacing[a[0]],y=point.y()/spacing[a[1]];
    if(!clamp&&(x<0||y<0||x>=d[a[0]]||y>=d[a[1]]))return false;
    q[a[0]]=static_cast<std::size_t>(std::clamp(std::floor(x),0.,double(d[a[0]]-1)));
    q[a[1]]=d[a[1]]-1-static_cast<std::size_t>(std::clamp(std::floor(y),0.,double(d[a[1]]-1)));q[a[2]]=sliders_[p]->value();return true;
}
void SliceViewer::refresh(){for(int p=0;p<3;++p)render(p);}
bool SliceViewer::setMask(std::shared_ptr<const Volume3D<float>> mask,const QString& name,QString* error) {
    mask_.reset();
    auto fail=[&](const QString& s){if(error)*error=s;maskStatus_->setText(s);refresh();return false;};
    if(!volume_||!mask)return fail("请先载入底图与有效 Mask");
    if(mask->dimensions()!=volume_->dimensions())return fail("Mask 尺寸与底图不同（ROI 结果请配对相同裁剪底图）");
    const auto& a=volume_->geometry();const auto& b=mask->geometry();
    for(int i=0;i<3;++i)if(std::abs(a.spacing[i]-b.spacing[i])>1e-5||std::abs(a.origin[i]-b.origin[i])>1e-4)return fail("Mask 间距或原点与底图不一致");
    for(int i=0;i<9;++i)if(std::abs(a.direction[i]-b.direction[i])>1e-6)return fail("Mask 方向与底图不一致");
    for(float v:mask->data())if(!std::isfinite(v)||(v!=0&&v!=1))return fail("Mask 必须为有限的 0/1 二值数据");
    mask_=std::move(mask);maskStatus_->setText("橙色 Mask："+name);refresh();return true;
}
void SliceViewer::loadMask(const QString& path) {
    if(!volume_||watcher_.isRunning()||maskWatcher_.isRunning())return;
    mask_.reset();refresh();maskButton_->setEnabled(false);maskStatus_->setText("正在读取 Mask："+path);setProperty("maskPath",path);
    maskWatcher_.setProperty("generation",QVariant::fromValue(generation_));
    maskWatcher_.setFuture(QtConcurrent::run([path]{LoadResult r;try {
#ifdef MEDVESSEL_GUI_NIFTI
        r.volume=std::make_shared<Volume3D<float>>(readNifti(path.toUtf8().toStdString()));
#else
        throw std::runtime_error("此构建未启用 NIfTI 读取");
#endif
    }catch(const std::exception& e){r.error=QString::fromUtf8(e.what());}return r;}));
}
bool SliceViewer::eventFilter(QObject* object,QEvent* event) {
    if(resultPicking_&&volume_&&event->type()==QEvent::MouseButtonPress){
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton)for(int p=0;p<3;++p)if(object==views_[p]->viewport()){
            std::array<std::size_t,3> q{};
            if(voxelAt(p,mouse->position().toPoint(),q)){
                auto world=volume_->geometry().indexToWorld({double(q[0]),double(q[1]),double(q[2])});
                if(focusWorld(world))emit worldPointPicked(world[0],world[1],world[2]);
            }
            return true;
        }
    }
    for(int p=0;p<3;++p)if(views_[p]&&object==views_[p]->viewport()&&pickingEnabled_&&pickMode_->currentIndex()!=0&&volume_) {
        if(event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)break;std::array<std::size_t,3> q{};if(!voxelAt(p,mouse->position().toPoint(),q))return true;
            if(pickMode_->currentIndex()==1){emit seedPicked(QString("%1,%2,%3").arg(q[0]).arg(q[1]).arg(q[2]));return true;}
            dragPlane_=p;dragSlice_=sliders_[p]->value();dragVoxel_=q;dragStart_=mouse->position().toPoint();delete rubber_;rubber_=new QRubberBand(QRubberBand::Rectangle,views_[p]->viewport());rubber_->setGeometry(QRect(dragStart_,QSize()));rubber_->show();return true;
        }
        if(event->type()==QEvent::MouseMove&&dragPlane_==p){auto* mouse=static_cast<QMouseEvent*>(event);rubber_->setGeometry(QRect(dragStart_,mouse->position().toPoint()).normalized());return true;}
        if(event->type()==QEvent::MouseButtonRelease&&dragPlane_==p){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)break;rubber_->hide();dragPlane_=-1;std::array<std::size_t,3> q{};if(dragSlice_!=sliders_[p]->value()||!voxelAt(p,mouse->position().toPoint(),q,true))return true;
            auto d=volume_->dimensions();std::array<std::size_t,6> r=roi_.value_or(std::array<std::size_t,6>{0,0,0,d[0],d[1],d[2]});for(int axis:{axes[p][0],axes[p][1]}){r[axis]=std::min(q[axis],dragVoxel_[axis]);r[axis+3]=std::max(q[axis],dragVoxel_[axis])-r[axis]+1;}
            emit roiPicked(QString("%1,%2,%3,%4,%5,%6").arg(r[0]).arg(r[1]).arg(r[2]).arg(r[3]).arg(r[4]).arg(r[5]));return true;
        }
    }
    if(event->type()==QEvent::Resize||event->type()==QEvent::Show)
        for(int p=0;p<3;++p)if(views_[p]&&object==views_[p]->viewport()&&!scenes_[p]->items().isEmpty())views_[p]->fitInView(scenes_[p]->sceneRect(),Qt::KeepAspectRatio);
    return QWidget::eventFilter(object,event);
}
void SliceViewer::load(const QString& path) {
    if(watcher_.isRunning()||maskWatcher_.isRunning()){status_->setText("正在读取影像或 Mask，请等待后再加载");return;}
    setProperty("loadingPath",path);status_->setText("正在后台读取："+path);
    ++generation_;focusWorld_.reset();focusVoxel_.reset();focusStatus_->clear();watcher_.setProperty("generation",QVariant::fromValue(generation_));
    // Clear previous imagery so a failed/new load cannot be mistaken for the selected input.
    dragPlane_=-1;if(rubber_)rubber_->hide();volume_.reset();mask_.reset();maskButton_->setEnabled(false);maskStatus_->setText("未载入 Mask（橙色）");for(int p=0;p<3;++p){scenes_[p]->clear();labels_[p]->clear();sliders_[p]->setEnabled(false);}
    watcher_.setFuture(QtConcurrent::run([path] {
        LoadResult result;
        try {
            if(QFileInfo(path).isDir()) {
#ifdef MEDVESSEL_GUI_DICOM
                result.volume=std::make_shared<Volume3D<float>>(readDicomSeries(std::filesystem::u8path(path.toUtf8().toStdString())));
#else
                throw std::runtime_error("此构建未启用 DICOM 读取");
#endif
            } else {
#ifdef MEDVESSEL_GUI_NIFTI
                result.volume=std::make_shared<Volume3D<float>>(readNifti(path.toUtf8().toStdString()));
#else
                throw std::runtime_error("此构建未启用 NIfTI 读取");
#endif
            }
            for(auto dim:result.volume->dimensions())if(dim>static_cast<std::size_t>(std::numeric_limits<int>::max()))throw std::runtime_error("影像尺寸超过显示范围");
        }catch(const std::exception& e){result.volume.reset();result.error=QString::fromUtf8(e.what());}
        return result;
    }));
}
void SliceViewer::setVolume(std::shared_ptr<const Volume3D<float>> volume,const QString& name) {
    focusVoxel_.reset();
    setProperty("volumePath",name);
    volume_=std::move(volume);mask_.reset();maskButton_->setEnabled(true);maskStatus_->setText("未载入 Mask（橙色）");minimum_=std::numeric_limits<double>::infinity();maximum_=-minimum_;
    for(float v:volume_->data())if(std::isfinite(v)){minimum_=std::min(minimum_,double(v));maximum_=std::max(maximum_,double(v));}
    if(!std::isfinite(minimum_)){minimum_=0;maximum_=1;}
    if(maximum_<=minimum_)maximum_=minimum_+1;
    {QSignalBlocker w(width_),l(level_);width_->setValue(maximum_-minimum_);level_->setValue((minimum_+maximum_)/2);}
    status_->setText(name+"\n原网格 XY / XZ / YZ，按物理间距显示，方向使用 RAS；斜采集不做解剖平面重采样。窗宽窗位仅改变显示；索引从 0 开始。");
    for(int p=0;p<3;++p){QSignalBlocker blocker(sliders_[p]);int count=int(volume_->dimensions()[axes[p][2]]);sliders_[p]->setRange(0,count-1);sliders_[p]->setValue(count/2);sliders_[p]->setEnabled(true);render(p);}
    setSelection(roiText_,seedsText_);
    focusVoxel_.reset();if(focusWorld_)focusWorld(*focusWorld_);
}
QImage SliceViewer::sliceImage(int plane,int index) const {
    if(!volume_||plane<0||plane>2)return {};
    auto d=volume_->dimensions();const auto* a=axes[plane];
    if(index<0||static_cast<std::size_t>(index)>=d[a[2]])return {};
    QImage image(int(d[a[0]]),int(d[a[1]]),QImage::Format_RGB32);
    if(image.isNull())return image;
    const double low=level_->value()-width_->value()/2,alpha=opacity_->value()/100.;
    for(int y=0;y<image.height();++y){auto* row=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x){std::array<std::size_t,3> q{};q[a[0]]=x;q[a[1]]=image.height()-1-y;q[a[2]]=index;double v=volume_->at(q[0],q[1],q[2]);int gray=std::isfinite(v)?int(std::clamp((v-low)/width_->value()*255.,0.,255.)):0;
        if(mask_&&overlay_->isChecked()&&mask_->at(q[0],q[1],q[2])!=0)row[x]=qRgb(int(gray*(1-alpha)+255*alpha),int(gray*(1-alpha)+128*alpha),int(gray*(1-alpha)));
        else row[x]=qRgb(gray,gray,gray);
    }}
    return image;
}
void SliceViewer::render(int p) {
    if(!volume_)return;
    const auto* a=axes[p];int index=sliders_[p]->value();auto image=sliceImage(p,index);scenes_[p]->clear();
    auto* item=scenes_[p]->addPixmap(QPixmap::fromImage(image));auto spacing=volume_->geometry().spacing;
    item->setTransform(QTransform::fromScale(spacing[a[0]],spacing[a[1]]));scenes_[p]->setSceneRect(item->sceneBoundingRect());views_[p]->fitInView(scenes_[p]->sceneRect(),Qt::KeepAspectRatio);
    QPen seedPen(QColor("#00ffff"));seedPen.setCosmetic(true);seedPen.setWidth(2);QPen roiPen(QColor("#65ff65"));roiPen.setCosmetic(true);roiPen.setWidth(2);
    const auto d=volume_->dimensions();
    if(focusVoxel_&&(*focusVoxel_)[a[2]]==static_cast<std::size_t>(index)){const auto q=*focusVoxel_;QPen pen(QColor("#ffe14a"));pen.setCosmetic(true);pen.setStyle(Qt::DashLine);pen.setWidth(1);auto* horizontal=new QGraphicsLineItem(0,d[a[1]]-q[a[1]]-.5,d[a[0]],d[a[1]]-q[a[1]]-.5,item);horizontal->setPen(pen);auto* vertical=new QGraphicsLineItem(q[a[0]]+.5,0,q[a[0]]+.5,d[a[1]],item);vertical->setPen(pen);}
    for(const auto& q:seeds_)if(q[a[2]]==static_cast<std::size_t>(index)){double x=q[a[0]]+.5,y=d[a[1]]-q[a[1]]-.5;auto* mark=new QGraphicsEllipseItem(x-.4,y-.4,.8,.8,item);mark->setPen(seedPen);}
    if(roi_){const auto& r=*roi_;if(static_cast<std::size_t>(index)>=r[a[2]]&&static_cast<std::size_t>(index)-r[a[2]]<r[a[2]+3]){auto* box=new QGraphicsRectItem(r[a[0]],d[a[1]]-r[a[1]]-r[a[1]+3],r[a[0]+3],r[a[1]+3],item);box->setPen(roiPen);}}
    auto& g=volume_->geometry();Point3D center{};for(int i=0;i<3;++i)center[i]=(volume_->dimensions()[i]-1)/2.;center[a[2]]=index;auto world=g.indexToWorld(center);
    const char* names[]={"XY（通常轴位）","XZ（通常冠状位）","YZ（通常矢状位）"};
    labels_[p]->setText(QString("%1 · %2 / %3 · 左 %4 | 右 %5 | 上 %6 | 下 %7\n切片中心 RAS mm：%8, %9, %10").arg(names[p]).arg(index).arg(sliders_[p]->maximum()).arg(directionName(g,a[0],-1),directionName(g,a[0],1),directionName(g,a[1],1),directionName(g,a[1],-1)).arg(world[0],0,'f',1).arg(world[1],0,'f',1).arg(world[2],0,'f',1));
}
}

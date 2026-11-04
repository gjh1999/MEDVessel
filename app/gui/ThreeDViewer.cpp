#include "ThreeDViewer.hpp"
#include "JobConfig.hpp"
#include "io/Nifti.hpp"
#include <QtWidgets>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QVTKOpenGLNativeWidget.h>
#include <QVTKInteractor.h>
#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkFlyingEdges3D.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <algorithm>
#include <limits>

namespace medvessel::desktop {
ThreeDViewer::Scene ThreeDViewer::prepare(const QString& directory){
    Scene scene;
    try{
        const auto record=readJson(QDir(directory).filePath("run.json"));auto mask=readNifti(QDir(directory).filePath("mask.nii.gz").toUtf8().toStdString());
        auto d=mask.dimensions();auto dims=record["dimensions"].toArray(),spacing=record["spacing_mm"].toArray();
        if(dims.size()!=3||spacing.size()!=3)throw std::runtime_error("结果缺少网格信息");
        for(int i=0;i<3;++i)if(d[i]>std::size_t(std::numeric_limits<int>::max()-2)||dims[i].toDouble()!=double(d[i])||!std::isfinite(spacing[i].toDouble())||std::abs(spacing[i].toDouble()-mask.geometry().spacing[i])>1e-5)throw std::runtime_error("Mask 与运行记录网格不符");
        for(float v:mask.data())if(!std::isfinite(v)||(v!=0&&v!=1))throw std::runtime_error("Mask 必须是有限二值数据");
        scene.geometry=mask.geometry();scene.dimensions=d;
        vtkNew<vtkImageData> image;image->SetDimensions(int(d[0])+2,int(d[1])+2,int(d[2])+2);const auto& g=mask.geometry();image->SetSpacing(g.spacing.data());image->SetOrigin(-g.spacing[0],-g.spacing[1],-g.spacing[2]);image->AllocateScalars(VTK_UNSIGNED_CHAR,1);
        for(int z=0;z<int(d[2])+2;++z)for(int y=0;y<int(d[1])+2;++y)for(int x=0;x<int(d[0])+2;++x)*static_cast<unsigned char*>(image->GetScalarPointer(x,y,z))=(x>0&&y>0&&z>0&&x<=int(d[0])&&y<=int(d[1])&&z<=int(d[2]))?static_cast<unsigned char>(mask.at(x-1,y-1,z-1)):0;
        vtkNew<vtkFlyingEdges3D> contour;contour->SetInputData(image);contour->SetValue(0,.5);
        vtkNew<vtkMatrix4x4> matrix;matrix->Identity();for(int i=0;i<3;++i){for(int j=0;j<3;++j)matrix->SetElement(i,j,g.direction[i*3+j]);matrix->SetElement(i,3,g.origin[i]);}
        vtkNew<vtkTransform> transform;transform->SetMatrix(matrix);vtkNew<vtkTransformPolyDataFilter> world;world->SetTransform(transform);world->SetInputConnection(contour->GetOutputPort());
        vtkNew<vtkPolyDataNormals> normals;normals->SetInputConnection(world->GetOutputPort());normals->ConsistencyOn();normals->AutoOrientNormalsOn();normals->Update();scene.surface=vtkSmartPointer<vtkPolyData>::New();scene.surface->DeepCopy(normals->GetOutput());
        QFile csv(QDir(directory).filePath("centerline.csv"));if(!csv.open(QIODevice::ReadOnly))throw std::runtime_error("无法读取 centerline.csv");
        auto header=QString::fromUtf8(csv.readLine()).trimmed().split(',');const QStringList required{"branch_id","point_id","x_ras_mm","y_ras_mm","z_ras_mm"};std::array<int,5> columns{};for(int i=0;i<5;++i){columns[i]=header.indexOf(required[i]);if(columns[i]<0)throw std::runtime_error("中心线 CSV 表头不完整");}
        QMap<int,QMap<int,Point3D>> points;
        while(!csv.atEnd()){auto line=QString::fromUtf8(csv.readLine()).trimmed();if(line.isEmpty())continue;auto fields=line.split(',');if(fields.size()!=header.size())throw std::runtime_error("中心线 CSV 列数错误");bool ok;int branch=fields[columns[0]].toInt(&ok);if(!ok||branch<0)throw std::runtime_error("无效分支 ID");int point=fields[columns[1]].toInt(&ok);if(!ok||point<0||points[branch].contains(point))throw std::runtime_error("无效或重复点 ID");Point3D p{};for(int i=0;i<3;++i){p[i]=fields[columns[i+2]].toDouble(&ok);if(!ok||!std::isfinite(p[i]))throw std::runtime_error("中心线含无效坐标");}points[branch][point]=p;}
        if(record["branches"].toInt(-1)!=points.size())throw std::runtime_error("分支数量与运行记录不一致");
        for(auto it=points.cbegin();it!=points.cend();++it){vtkNew<vtkPoints> vertices;vertices->SetDataTypeToDouble();int expected=0;for(auto p=it->cbegin();p!=it->cend();++p){if(p.key()!=expected++)throw std::runtime_error("中心线点 ID 不连续");vertices->InsertNextPoint(p.value().data());}vtkNew<vtkCellArray> lines;lines->InsertNextCell(vertices->GetNumberOfPoints());for(vtkIdType i=0;i<vertices->GetNumberOfPoints();++i)lines->InsertCellPoint(i);auto poly=vtkSmartPointer<vtkPolyData>::New();poly->SetPoints(vertices);poly->SetLines(lines);scene.branches[it.key()]=poly;}
    }catch(const std::exception& e){scene={};scene.error=QString::fromUtf8(e.what());}return scene;
}
ThreeDViewer::ThreeDViewer(QWidget* parent):QWidget(parent){
    auto* layout=new QVBoxLayout(this);auto* bar=new QHBoxLayout;auto* loadButton=new QPushButton("载入三维结果");auto* reset=new QPushButton("重置视角");locate_=new QPushButton("定位到结果切片");locate_->setObjectName("locateSlices");locate_->setEnabled(false);bar->addWidget(loadButton);bar->addWidget(reset);bar->addWidget(locate_);layout->addLayout(bar);
    auto* surface=new QCheckBox("显示表面");surface->setChecked(true);auto* opacity=new QSlider(Qt::Horizontal);opacity->setRange(0,100);opacity->setValue(35);auto* appearance=new QHBoxLayout;appearance->addWidget(surface);appearance->addWidget(new QLabel("表面透明度"));appearance->addWidget(opacity);layout->addLayout(appearance);
    const auto platform=QGuiApplication::platformName();if(platform!="offscreen"&&platform!="minimal"){
        gl_=new QVTKOpenGLNativeWidget;layout->addWidget(gl_,1);vtkNew<vtkGenericOpenGLRenderWindow> window;gl_->setRenderWindow(window);renderer_=vtkSmartPointer<vtkRenderer>::New();renderer_->SetBackground(.04,.08,.12);window->AddRenderer(renderer_);gl_->installEventFilter(this);
        vtkNew<vtkAxesActor> axes;axes->SetXAxisLabelText("R");axes->SetYAxisLabelText("A");axes->SetZAxisLabelText("S");
        orientation_=vtkSmartPointer<vtkOrientationMarkerWidget>::New();orientation_->SetOrientationMarker(axes);orientation_->SetInteractor(gl_->interactor());orientation_->SetViewport(0,0,.2,.2);orientation_->SetEnabled(1);orientation_->InteractiveOff();
    }else layout->addWidget(new QLabel("当前平台仅验证三维几何与联动，不创建 OpenGL 窗口"));
    status_=new QLabel("打开结果后载入三维血管 · 世界坐标 R/A/S（mm）");status_->setWordWrap(true);layout->addWidget(status_);
    connect(loadButton,&QPushButton::clicked,this,&ThreeDViewer::load);connect(reset,&QPushButton::clicked,this,[this]{if(renderer_){renderer_->ResetCamera();render();}});connect(locate_,&QPushButton::clicked,this,&ThreeDViewer::locateSlicesRequested);
    connect(surface,&QCheckBox::toggled,this,[this](bool visible){if(surfaceActor_){surfaceActor_->SetVisibility(visible);render();}});connect(opacity,&QSlider::valueChanged,this,[this](int value){if(surfaceActor_){surfaceActor_->GetProperty()->SetOpacity(value/100.);render();}});
    setProperty("surfaceVisible",true);setProperty("surfaceOpacity",.35);
    connect(surface,&QCheckBox::toggled,this,[this](bool value){setProperty("surfaceVisible",value);});connect(opacity,&QSlider::valueChanged,this,[this](int value){setProperty("surfaceOpacity",value/100.);});
}
void ThreeDViewer::setResult(const QString& directory){++generation_;directory_=directory;scene_={};actors_.clear();surfaceActor_=nullptr;targetActor_=nullptr;selected_=-1;locate_->setEnabled(false);if(renderer_)renderer_->RemoveAllViewProps();emit locationCleared();render();}
void ThreeDViewer::load(){
    const auto directory=directory_;setResult(directory);const auto token=generation_;status_->setText("正在构建三维几何…");auto* watcher=new QFutureWatcher<Scene>(this);
    connect(watcher,&QFutureWatcher<Scene>::finished,this,[this,watcher,token]{auto scene=watcher->result();watcher->deleteLater();if(token!=generation_)return;if(!scene.error.isEmpty()){status_->setText(scene.error);emit loaded(false);return;}scene_=std::move(scene);
        if(renderer_){auto add=[this](vtkPolyData* data){vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(data);mapper->ScalarVisibilityOff();auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(mapper);renderer_->AddActor(actor);return actor;};surfaceActor_=add(scene_.surface);surfaceActor_->GetProperty()->SetColor(.85,.3,.2);surfaceActor_->GetProperty()->SetAmbient(.2);surfaceActor_->GetProperty()->SetOpacity(property("surfaceOpacity").toDouble());surfaceActor_->SetVisibility(property("surfaceVisible").toBool());
            for(auto it=scene_.branches.cbegin();it!=scene_.branches.cend();++it){auto actor=add(it.value());actor->GetProperty()->SetColor(.1,.9,.8);actor->GetProperty()->SetLineWidth(3);actor->GetProperty()->LightingOff();actors_[it.key()]=actor;}
            if(scene_.surface->GetNumberOfPoints()>0){double bounds[6];scene_.surface->GetBounds(bounds);Point3D center{};double extent=1;for(int i=0;i<3;++i){center[i]=(bounds[2*i]+bounds[2*i+1])/2;extent=std::max(extent,bounds[2*i+1]-bounds[2*i]);}auto* camera=renderer_->GetActiveCamera();camera->SetFocalPoint(center.data());camera->SetPosition(center[0]+extent*1.5,center[1]-extent*1.5,center[2]+extent);camera->SetViewUp(0,0,1);}
            renderer_->ResetCamera();render();}
        status_->setText(QString("已载入 %1 条分支 · 单击中心线选取，拖动旋转").arg(scene_.branches.size()));emit loaded(true);});
    watcher->setFuture(QtConcurrent::run([directory]{return prepare(directory);}));
}
void ThreeDViewer::render(){if(gl_&&gl_->isVisible()&&gl_->isValid())gl_->renderWindow()->Render();}
void ThreeDViewer::moveFocus(const Point3D& p){
    if(renderer_){auto* camera=renderer_->GetActiveCamera();double old[3],position[3];camera->GetFocalPoint(old);camera->GetPosition(position);camera->SetFocalPoint(p.data());camera->SetPosition(position[0]+p[0]-old[0],position[1]+p[1]-old[1],position[2]+p[2]-old[2]);renderer_->ResetCameraClippingRange();}render();
}
void ThreeDViewer::selectBranch(int id){
    if(targetActor_)targetActor_->VisibilityOff();
    selected_=scene_.branches.contains(id)?id:-1;for(auto it=actors_.cbegin();it!=actors_.cend();++it){auto* p=it.value()->GetProperty();if(it.key()==selected_){p->SetColor(1,.5,0);p->SetLineWidth(6);}else{p->SetColor(.1,.9,.8);p->SetLineWidth(3);}}
    locate_->setEnabled(selected_>=0);if(selected_<0){emit locationCleared();render();return;}auto poly=scene_.branches[selected_];if(poly->GetNumberOfPoints()==0)return;Point3D point{};poly->GetPoint(poly->GetNumberOfPoints()/2,point.data());moveFocus(point);emit branchLocationChanged(point[0],point[1],point[2]);
}
bool ThreeDViewer::focusWorld(const Point3D& world,QString* error){
    auto fail=[&](const QString& message){if(error)*error=message;status_->setText(message);return false;};for(double v:world)if(!std::isfinite(v))return fail("定位坐标无效");if(!scene_.surface)return fail("请先载入三维结果");
    auto index=scene_.geometry.worldToIndex(world);
    for(int i=0;i<3;++i)if(index[i]<-1e-3||index[i]>double(scene_.dimensions[i]-1)+1e-3)return fail("定位坐标超出结果网格");
    // A separate marker preserves the selected branch and avoids a feedback loop.
    if(renderer_){if(!targetActor_){vtkNew<vtkSphereSource> sphere;sphere->SetRadius(.5);sphere->SetThetaResolution(16);sphere->SetPhiResolution(16);vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputConnection(sphere->GetOutputPort());targetActor_=vtkSmartPointer<vtkActor>::New();targetActor_->SetMapper(mapper);targetActor_->GetProperty()->SetColor(1,1,0);targetActor_->PickableOff();renderer_->AddActor(targetActor_);}targetActor_->SetPosition(world[0],world[1],world[2]);targetActor_->VisibilityOn();}moveFocus(world);status_->setText(QString("切片定位 RAS (%1, %2, %3) mm").arg(world[0]).arg(world[1]).arg(world[2]));return true;
}
bool ThreeDViewer::eventFilter(QObject* object,QEvent* event){
    if(object==gl_&&event->type()==QEvent::MouseButtonPress){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton)press_=mouse->position().toPoint();}
    if(object==gl_&&event->type()==QEvent::MouseButtonRelease){auto* mouse=static_cast<QMouseEvent*>(event);auto point=mouse->position().toPoint();if(mouse->button()==Qt::LeftButton&&(point-press_).manhattanLength()<=4&&renderer_){vtkNew<vtkCellPicker> picker;picker->PickFromListOn();picker->SetTolerance(.015);for(const auto& actor:actors_)picker->AddPickList(actor);double ratio=gl_->devicePixelRatioF();if(picker->Pick(point.x()*ratio,(gl_->height()-1-point.y())*ratio,0,renderer_)){for(auto it=actors_.cbegin();it!=actors_.cend();++it)if(it.value()==picker->GetActor()){emit branchPicked(it.key());selectBranch(it.key());auto* p=picker->GetPickPosition();emit branchLocationChanged(p[0],p[1],p[2]);break;}}}}
    return QWidget::eventFilter(object,event);
}
}

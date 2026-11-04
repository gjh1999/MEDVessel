#include "ThreeDViewer.hpp"
#include "JobConfig.hpp"
#include "io/Nifti.hpp"
#include <QJsonArray>
#include <QtWidgets>
#include <QtConcurrent/QtConcurrentRun>
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
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkSphereSource.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <climits>
#include <cstring>
namespace medvessel::desktop {
ThreeDScene ThreeDViewer::prepare(const QString& directory) {
    ThreeDScene scene;
    try {
        QDir dir(directory);auto run=readJson(dir.filePath("run.json"));
        const int count=run["branches"].toInt(-1);if(count<0)throw std::runtime_error("run.json 缺少分支数量");
        auto mask=readNifti(dir.filePath("mask.nii.gz").toUtf8().toStdString());auto d=mask.dimensions();auto g=mask.geometry();
        auto dims=run["dimensions"].toArray(),spacing=run["spacing_mm"].toArray();
        if(run["schema_version"].toInt()!=1||dims.size()!=3||spacing.size()!=3)throw std::runtime_error("运行记录缺少网格信息");
        for(int a=0;a<3;++a)if(dims[a].toDouble(-1)!=double(d[a])||std::abs(spacing[a].toDouble(-1)-g.spacing[a])>1e-5)throw std::runtime_error("Mask 网格与运行记录不符");
        for(auto size:d)if(size>INT_MAX-2)throw std::runtime_error("Mask 尺寸超过 VTK 范围");
        // Pad one background voxel on each side so surfaces close at volume edges.
        vtkNew<vtkImageData> image;image->SetDimensions(int(d[0])+2,int(d[1])+2,int(d[2])+2);image->SetSpacing(g.spacing.data());image->SetOrigin(-g.spacing[0],-g.spacing[1],-g.spacing[2]);image->AllocateScalars(VTK_UNSIGNED_CHAR,1);
        std::memset(image->GetScalarPointer(),0,static_cast<std::size_t>(image->GetNumberOfPoints()));
        for(std::size_t z=0;z<d[2];++z)for(std::size_t y=0;y<d[1];++y)for(std::size_t x=0;x<d[0];++x){float v=mask.at(x,y,z);if(!std::isfinite(v)||(v!=0&&v!=1))throw std::runtime_error("Mask 必须为有限的 0/1 数据");*static_cast<unsigned char*>(image->GetScalarPointer(int(x)+1,int(y)+1,int(z)+1))=static_cast<unsigned char>(v);}
        vtkNew<vtkFlyingEdges3D> contour;contour->SetInputData(image);contour->SetValue(0,.5);
        vtkNew<vtkMatrix4x4> matrix;matrix->Identity();for(int i=0;i<3;++i){for(int j=0;j<3;++j)matrix->SetElement(i,j,g.direction[i*3+j]);matrix->SetElement(i,3,g.origin[i]);}
        vtkNew<vtkTransform> transform;transform->SetMatrix(matrix);vtkNew<vtkTransformPolyDataFilter> world;world->SetTransform(transform);world->SetInputConnection(contour->GetOutputPort());world->Update();
        vtkNew<vtkPolyDataNormals> normals;normals->SetInputConnection(world->GetOutputPort());normals->AutoOrientNormalsOn();normals->ConsistencyOn();normals->SplittingOff();normals->Update();
        scene.surface=vtkSmartPointer<vtkPolyData>::New();scene.surface->DeepCopy(normals->GetOutput());
        QFile csv(dir.filePath("centerline.csv"));if(!csv.open(QIODevice::ReadOnly))throw std::runtime_error("无法读取 centerline.csv");
        QTextStream stream(&csv);if(stream.readLine().trimmed()!="branch_id,point_id,x_ras_mm,y_ras_mm,z_ras_mm")throw std::runtime_error("中心线表头不支持");
        QMap<int,vtkSmartPointer<vtkPoints>> points;
        while(!stream.atEnd()) {auto line=stream.readLine().trimmed();if(line.isEmpty())continue;auto cells=line.split(',');if(cells.size()!=5)throw std::runtime_error("中心线行格式错误");bool ok=false;int id=cells[0].toInt(&ok);if(!ok||id<0||id>=count)throw std::runtime_error("中心线分支 ID 无效");int index=cells[1].toInt(&ok);if(!ok||index<0)throw std::runtime_error("中心线点 ID 无效");if(!points.contains(id)){points[id]=vtkSmartPointer<vtkPoints>::New();points[id]->SetDataTypeToDouble();}if(index!=points[id]->GetNumberOfPoints())throw std::runtime_error("中心线点 ID 不连续或重复");double p[3];for(int a=0;a<3;++a){p[a]=cells[a+2].toDouble(&ok);if(!ok||!std::isfinite(p[a]))throw std::runtime_error("中心线坐标无效");}points[id]->InsertNextPoint(p);}
        if(points.size()!=count)throw std::runtime_error("中心线分支数量与运行记录不符");
        for(auto it=points.cbegin();it!=points.cend();++it){auto data=vtkSmartPointer<vtkPolyData>::New();data->SetPoints(it.value());vtkNew<vtkCellArray> lines;const auto n=it.value()->GetNumberOfPoints();if(n<2)throw std::runtime_error("中心线分支点数不足");lines->InsertNextCell(n);for(vtkIdType i=0;i<n;++i)lines->InsertCellPoint(i);data->SetLines(lines);scene.branches[it.key()]=data;}
    }catch(const std::exception& e){scene.surface=nullptr;scene.branches.clear();scene.error=QString::fromUtf8(e.what());}
    return scene;
}
ThreeDViewer::ThreeDViewer(QWidget* parent):QWidget(parent) {
    auto* root=new QVBoxLayout(this);auto* row=new QHBoxLayout;auto* loadButton=new QPushButton("载入三维结果");loadButton->setObjectName("load3D");auto* reset=new QPushButton("重置视角");row->addWidget(loadButton);row->addWidget(reset);
    auto* visible=new QCheckBox("显示血管表面");visible->setChecked(true);row->addWidget(visible);auto* opacity=new QSlider(Qt::Horizontal);opacity->setRange(0,100);opacity->setValue(25);row->addWidget(new QLabel("表面不透明度"));row->addWidget(opacity);root->addLayout(row);
    status_=new QLabel("打开分析结果后，点击载入三维结果。坐标为 RAS 毫米；左键旋转，滚轮缩放，单击中心线选择分支。");status_->setWordWrap(true);root->addWidget(status_);
    locate_=new QPushButton("定位到结果切片");locate_->setObjectName("locateSlices");locate_->setEnabled(false);root->addWidget(locate_);connect(locate_,&QPushButton::clicked,this,&ThreeDViewer::locateSlicesRequested);
    renderer_=vtkSmartPointer<vtkRenderer>::New();renderer_->SetBackground(.045,.075,.11);window_=vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();window_->AddRenderer(renderer_);window_->SetMultiSamples(0);
    // Qt's offscreen platform cannot create QOpenGLWidget contexts. Geometry and linkage remain testable.
    if(QGuiApplication::platformName()!="offscreen"&&QGuiApplication::platformName()!="minimal") {view_=new QVTKOpenGLNativeWidget;view_->setObjectName("vtkView");view_->setRenderWindow(window_);view_->installEventFilter(this);root->addWidget(view_,1);vtkNew<vtkInteractorStyleTrackballCamera> style;view_->interactor()->SetInteractorStyle(style);}
    else root->addWidget(new QLabel("当前 Qt 平台不提供 OpenGL 窗口；仅验证几何与分支联动。"),1);
    vtkNew<vtkSphereSource> marker;marker->SetRadius(.5);marker->SetThetaResolution(20);marker->SetPhiResolution(20);
    vtkNew<vtkPolyDataMapper> markerMapper;markerMapper->SetInputConnection(marker->GetOutputPort());
    focusMarker_=vtkSmartPointer<vtkActor>::New();focusMarker_->SetMapper(markerMapper);focusMarker_->GetProperty()->SetColor(1,1,0);focusMarker_->PickableOff();focusMarker_->VisibilityOff();
    surface_=vtkSmartPointer<vtkActor>::New();surface_->GetProperty()->SetColor(.6,.8,.85);surface_->GetProperty()->SetOpacity(.25);surface_->PickableOff();
    connect(loadButton,&QPushButton::clicked,this,&ThreeDViewer::load);connect(reset,&QPushButton::clicked,this,[this]{renderer_->ResetCamera();render();});
    connect(visible,&QCheckBox::toggled,this,[this](bool checked){surface_->SetVisibility(checked);render();});connect(opacity,&QSlider::valueChanged,this,[this](int value){surface_->GetProperty()->SetOpacity(value/100.);render();});
    connect(&watcher_,&QFutureWatcher<ThreeDScene>::finished,this,[this]{
        const auto requested=watcher_.property("generation").toULongLong();if(requested!=generation_)return;auto scene=watcher_.result();
        if(!scene.error.isEmpty()){status_->setText("三维载入失败："+scene.error);emit loaded(false);return;}
        vtkNew<vtkPolyDataMapper> mapper;mapper->SetInputData(scene.surface);mapper->ScalarVisibilityOff();surface_->SetMapper(mapper);renderer_->AddActor(surface_);
        for(auto it=scene.branches.cbegin();it!=scene.branches.cend();++it){vtkNew<vtkPolyDataMapper> line;line->SetInputData(it.value());auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(line);actor->GetProperty()->SetColor(.15,.95,.8);actor->GetProperty()->SetLineWidth(3);actors_[it.key()]=actor;renderer_->AddActor(actor);}
        renderer_->GetActiveCamera()->SetPosition(1,-1,.7);renderer_->GetActiveCamera()->SetFocalPoint(0,0,0);renderer_->GetActiveCamera()->SetViewUp(0,0,1);renderer_->ResetCamera();auto pendingFocus=focusWorld_;if(pendingFocus){QSignalBlocker blocker(this);selectBranch(selected_);}else selectBranch(selected_);if(pendingFocus)focusWorld(*pendingFocus);status_->setText(QString("RAS 毫米 · %1 条中心线分支 · 表格选中会高亮并定位；单击中心线可回选表格。表面为 Mask 的 0.5 等值面。").arg(actors_.size()));render();emit loaded(true);
    });
}
ThreeDViewer::~ThreeDViewer()=default;
void ThreeDViewer::render(){if(view_&&view_->isVisible()&&view_->isValid()){
    if(!orientation_){vtkNew<vtkAxesActor> axes;axes->SetXAxisLabelText("R");axes->SetYAxisLabelText("A");axes->SetZAxisLabelText("S");orientation_=vtkSmartPointer<vtkOrientationMarkerWidget>::New();orientation_->SetOrientationMarker(axes);orientation_->SetInteractor(view_->interactor());orientation_->SetViewport(0,0,.2,.2);orientation_->SetEnabled(1);orientation_->InteractiveOff();}
    window_->Render();
}}
void ThreeDViewer::showEvent(QShowEvent* event){QWidget::showEvent(event);QTimer::singleShot(0,this,[this]{render();});}
void ThreeDViewer::setResult(const QString& directory){focusWorld_.reset();focusMarker_->VisibilityOff();++generation_;directory_=directory;locate_->setEnabled(false);actors_.clear();renderer_->RemoveAllViewProps();selected_=-1;status_->setText(directory.isEmpty()?"尚无结果":"三维结果待载入："+directory);render();}
void ThreeDViewer::load(){if(directory_.isEmpty()||watcher_.isRunning())return;renderer_->RemoveAllViewProps();actors_.clear();locate_->setEnabled(false);render();status_->setText("正在后台构建三维几何…");watcher_.setProperty("generation",QVariant::fromValue(generation_));const auto directory=directory_;watcher_.setFuture(QtConcurrent::run([directory]{return prepare(directory);}));}
void ThreeDViewer::selectBranch(int id){if(actors_.contains(id)){focusWorld_.reset();focusMarker_->VisibilityOff();}selected_=id;locate_->setEnabled(actors_.contains(id));if(!actors_.contains(id)&&!focusWorld_)emit locationCleared();for(auto it=actors_.begin();it!=actors_.end();++it){const bool selected=it.key()==id;it.value()->GetProperty()->SetColor(selected?1.:.15,selected?.45:.95,selected?.1:.8);it.value()->GetProperty()->SetLineWidth(selected?6:3);}if(actors_.contains(id)){auto* data=actors_[id]->GetMapper()->GetInput();double sample[3];data->GetPoint(data->GetNumberOfPoints()/2,sample);emit branchLocationChanged(sample[0],sample[1],sample[2]);auto* camera=renderer_->GetActiveCamera();const double* center=actors_[id]->GetCenter();double focal[3];camera->GetFocalPoint(focal);double pos[3];camera->GetPosition(pos);for(int a=0;a<3;++a)pos[a]+=center[a]-focal[a];camera->SetFocalPoint(center);camera->SetPosition(pos);renderer_->ResetCameraClippingRange();}render();}
bool ThreeDViewer::focusWorld(const Point3D& world){
    for(double value:world)if(!std::isfinite(value))return false;
    if(directory_.isEmpty())return false;
    focusWorld_=world;
    if(!renderer_->HasViewProp(surface_))return true;
    focusMarker_->SetPosition(world[0],world[1],world[2]);focusMarker_->VisibilityOn();renderer_->AddActor(focusMarker_);
    auto* camera=renderer_->GetActiveCamera();double focal[3],position[3];camera->GetFocalPoint(focal);camera->GetPosition(position);
    for(int a=0;a<3;++a)position[a]+=world[a]-focal[a];
    camera->SetFocalPoint(world.data());camera->SetPosition(position);renderer_->ResetCameraClippingRange();render();return true;
}
bool ThreeDViewer::eventFilter(QObject* object,QEvent* event){if(object==view_){if(event->type()==QEvent::MouseButtonPress)press_=static_cast<QMouseEvent*>(event)->position().toPoint();if(event->type()==QEvent::MouseButtonRelease){auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()==Qt::LeftButton&&(mouse->position().toPoint()-press_).manhattanLength()<4){vtkNew<vtkCellPicker> picker;picker->SetTolerance(.015);picker->PickFromListOn();for(auto actor:actors_)picker->AddPickList(actor);double ratio=view_->devicePixelRatioF();picker->Pick(mouse->position().x()*ratio,(view_->height()-1-mouse->position().y())*ratio,0,renderer_);for(auto it=actors_.cbegin();it!=actors_.cend();++it)if(it.value()==picker->GetActor()){selectBranch(it.key());emit branchPicked(it.key());const double* picked=picker->GetPickPosition();emit branchLocationChanged(picked[0],picked[1],picked[2]);break;}}}}return QWidget::eventFilter(object,event);}
}

#include "MainWindow.hpp"
#include "SliceViewer.hpp"
#include "core/Crop.hpp"
#ifdef MEDVESSEL_GUI_VTK
#include "ThreeDViewer.hpp"
#include <QVTKOpenGLNativeWidget.h>
#include <QSurfaceFormat>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <QTabWidget>
#endif
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsView>
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTableView>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QJsonArray>
#ifdef MEDVESSEL_GUI_NIFTI
#include "io/Nifti.hpp"
#endif

using namespace medvessel::desktop;
class GuiTests:public QObject {
    Q_OBJECT
    QTemporaryDir temp_;
    QString source_,cli_,completed_;
    JobConfig base() const {JobConfig c;c.input=source_;c.outputRoot=temp_.path()+"/results with spaces";c.executable=cli_;c.threads=2;return c;}
private slots:
#ifdef MEDVESSEL_GUI_NIFTI
    void worldFocusAndResultRoi() {
        auto dir=temp_.path()+"/result roi";QVERIFY(QDir().mkpath(dir));
        medvessel::ImageGeometry g;g.spacing={.5,.7,2};g.origin={10,20,30};g.direction={-1,0,0,0,0,-1,0,1,0};
        medvessel::Volume3D<float> source(7,8,9,g);for(int z=0;z<9;++z)for(int y=0;y<8;++y)for(int x=0;x<7;++x)source.at(x,y,z)=x+10*y+100*z;
        auto crop=medvessel::cropVolume(source,{1,2,3},{4,4,4});medvessel::Volume3D<float> mask(4,4,4,crop.geometry());mask.at(1,2,1)=1;
        auto input=dir+"/source.nii.gz";medvessel::writeNifti(source,input.toUtf8().toStdString());medvessel::writeNifti(mask,(dir+"/mask.nii.gz").toUtf8().toStdString());
        QJsonObject record{{"schema_version",1},{"input_path",input},{"roi_voxel",QJsonArray{1,2,3,4,4,4}},{"dimensions",QJsonArray{4,4,4}},{"spacing_mm",QJsonArray{.5,.7,2}}};QString error;QVERIFY(writeJson(dir+"/run.json",record,&error));
        SliceViewer viewer;viewer.setReadOnly();auto world=g.indexToWorld({2,4,4});QVERIFY(viewer.focusWorld(world));QSignalSpy loaded(&viewer,&SliceViewer::loaded);viewer.loadResult(dir);QTRY_COMPARE(loaded.count(),1);QVERIFY(loaded.first().first().toBool());
        QCOMPARE(viewer.findChild<QSlider*>("slice0")->value(),1);QCOMPARE(viewer.findChild<QSlider*>("slice1")->value(),2);QCOMPARE(viewer.findChild<QSlider*>("slice2")->value(),1);
        viewer.findChild<QCheckBox*>("showMask")->setChecked(false);QCOMPARE(qGray(viewer.sliceImage(0,1).pixel(1,1)),int(121./333*255));
        QVERIFY(!viewer.focusWorld({10000,20000,30000},&error));QVERIFY(!error.isEmpty());QVERIFY(viewer.focusWorld(world));
        record["input_path"]=dir+"/missing.nii.gz";QVERIFY(writeJson(dir+"/run.json",record,&error));loaded.clear();viewer.loadResult(dir);QTRY_COMPARE(loaded.count(),1);QVERIFY(loaded.first().first().toBool());QVERIFY(viewer.property("volumePath").toString().contains("仅显示结果 Mask"));
        QCOMPARE(viewer.findChild<QSlider*>("slice2")->value(),1);
        loaded.clear();viewer.loadResult(dir);viewer.clear();viewer.loadResult(dir+"/missing-result");QTRY_COMPARE(loaded.count(),1);QVERIFY(!loaded.first().first().toBool());QVERIFY(viewer.sliceImage(0,0).isNull());
    }
#endif
#ifdef MEDVESSEL_GUI_VTK
    void threeDGeometryAndBranchLinkage() {
        auto dir=temp_.path()+"/3d geometry";QVERIFY(QDir().mkpath(dir));
        medvessel::ImageGeometry g;g.spacing={.5,.7,2};g.origin={10,20,30};g.direction={-1,0,0,0,0,-1,0,1,0};
        medvessel::Volume3D<float> mask(3,4,5,g);mask.at(1,2,3)=1;medvessel::writeNifti(mask,(dir+"/mask.nii.gz").toUtf8().toStdString());
        QString error;QVERIFY(writeJson(dir+"/run.json",{{"schema_version",1},{"branches",2},{"dimensions",QJsonArray{3,4,5}},{"spacing_mm",QJsonArray{.5,.7,2}}},&error));
        auto write=[&](const QString& name,const QByteArray& content){QFile f(dir+"/"+name);return f.open(QIODevice::WriteOnly)&&f.write(content)==content.size();};
        QVERIFY(write("centerline.csv","branch_id,point_id,x_ras_mm,y_ras_mm,z_ras_mm\n0,0,9.5,14,31.4\n0,1,9.5,15,31.4\n1,0,9.5,14,31.4\n1,1,9,14,31.4\n"));
        auto scene=ThreeDViewer::prepare(dir);QVERIFY2(scene.error.isEmpty(),qPrintable(scene.error));QCOMPARE(scene.branches.size(),2);QVERIFY(scene.surface->GetNumberOfCells()>0);
        double bounds[6];scene.surface->GetBounds(bounds);const double expected[]={9.25,9.75,13,15,31.05,31.75};for(int i=0;i<6;++i)QVERIFY(std::abs(bounds[i]-expected[i])<1e-4);
        scene.branches[0]->GetBounds(bounds);QVERIFY(std::abs(bounds[0]-9.5)<1e-8);QVERIFY(std::abs(bounds[3]-15)<1e-8);
        QVERIFY(write("branches.csv","branch_id,start_node_id,end_node_id,length_mm,mean_edt_diameter_mm\n0,0,1,1,1\n1,0,2,0.5,1\n"));
        QVERIFY(write("preview.svg","<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='10' height='10'/></svg>"));
        MainWindow window;QVERIFY2(window.openResult(dir,&error),qPrintable(error));auto* viewer=window.findChild<ThreeDViewer*>();QVERIFY(viewer);
        QSignalSpy ready(viewer,&ThreeDViewer::loaded);viewer->load();QTRY_COMPARE(ready.count(),1);QVERIFY(ready.first().first().toBool());QCOMPARE(viewer->branchCount(),2);
        auto* table=window.findChild<QTableView*>("branches");table->sortByColumn(0,Qt::DescendingOrder);table->selectRow(0);QCOMPARE(viewer->selectedBranch(),1);
        auto originalConfig=window.config().toJson();auto* resultSlices=window.findChild<SliceViewer*>("resultSlices");QVERIFY(resultSlices);QSignalSpy slicesReady(resultSlices,&SliceViewer::loaded);
        QTest::mouseClick(viewer->findChild<QPushButton*>("locateSlices"),Qt::LeftButton);QTRY_COMPARE(slicesReady.count(),1);QVERIFY(slicesReady.first().first().toBool());
        QCOMPARE(resultSlices->findChild<QSlider*>("slice0")->value(),3);QCOMPARE(resultSlices->findChild<QSlider*>("slice1")->value(),2);QCOMPARE(resultSlices->findChild<QSlider*>("slice2")->value(),2);QCOMPARE(window.config().toJson(),originalConfig);
        if(qEnvironmentVariableIsSet("MEDVESSEL_TEST_OPENGL")){
            window.show();qobject_cast<QTabWidget*>(viewer->parentWidget()->parentWidget())->setCurrentWidget(viewer);QTest::qWait(500);
            auto* gl=viewer->findChild<QVTKOpenGLNativeWidget*>();QVERIFY(gl);QVERIFY(gl->isValid());
            auto* renderer=gl->renderWindow()->GetRenderers()->GetFirstRenderer();renderer->SetWorldPoint(9.5,14.8,31.4,1);renderer->WorldToDisplay();auto* display=renderer->GetDisplayPoint();double ratio=gl->devicePixelRatioF();
            QSignalSpy picked(viewer,&ThreeDViewer::branchPicked);QTest::mouseClick(gl,Qt::LeftButton,Qt::NoModifier,QPoint(int(display[0]/ratio),gl->height()-1-int(display[1]/ratio)));
            QTRY_COMPARE(picked.count(),1);QCOMPARE(picked.first().first().toInt(),0);
        }else viewer->branchPicked(0);
        QCOMPARE(table->model()->index(table->currentIndex().row(),0).data().toInt(),0);QCOMPARE(viewer->selectedBranch(),0);
        QCOMPARE(resultSlices->findChild<QSlider*>("slice2")->value(),1);QCOMPARE(window.config().toJson(),originalConfig);
        auto shot=qEnvironmentVariable("MEDVESSEL_RESULT_SLICES_SCREENSHOT");if(!shot.isEmpty()){window.show();qobject_cast<QTabWidget*>(viewer->parentWidget()->parentWidget())->setCurrentWidget(resultSlices);QTest::qWait(50);QVERIFY(window.grab().save(shot));}
        QVERIFY(write("centerline.csv","branch_id,point_id,x_ras_mm,y_ras_mm,z_ras_mm\n0,0,nan,14,31.4\n"));QVERIFY(!ThreeDViewer::prepare(dir).error.isEmpty());
        viewer->setResult(dir);QCOMPARE(viewer->branchCount(),0);ready.clear();viewer->load();QTRY_COMPARE(ready.count(),1);QVERIFY(!ready.first().first().toBool());QCOMPARE(viewer->branchCount(),0);
    }
#endif
    void mouseSelectionAcrossPlanes() {
        medvessel::ImageGeometry g;g.spacing={0.5,0.7,2};g.direction={-1,0,0,0,1,0,0,0,-1};
        auto volume=std::make_shared<medvessel::Volume3D<float>>(9,11,7,g);
        SliceViewer viewer;viewer.resize(800,1000);viewer.setVolume(volume,"selection");viewer.show();QTest::qWait(50);
        auto* mode=viewer.findChild<QComboBox*>("pickMode");mode->setCurrentIndex(1);
        QSignalSpy seeds(&viewer,&SliceViewer::seedPicked),rois(&viewer,&SliceViewer::roiPicked);
        const int a[3][3]={{0,1,2},{0,2,1},{1,2,0}};const std::array<int,3> d{9,11,7};
        for(int p=0;p<3;++p){auto* view=viewer.findChild<QGraphicsView*>(QString("sliceView%1").arg(p));auto* slider=viewer.findChild<QSlider*>(QString("slice%1").arg(p));slider->setValue(2);
            std::array<int,3> q{3,4,5};q[a[p][2]]=2;
            auto point=view->mapFromScene((q[a[p][0]]+.5)*g.spacing[a[p][0]],(d[a[p][1]]-q[a[p][1]]-.5)*g.spacing[a[p][1]]);
            QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,point);
            QCOMPARE(seeds.count(),p+1);QCOMPARE(seeds.last().first().toString(),QString("%1,%2,%3").arg(q[0]).arg(q[1]).arg(q[2]));
        }
        auto* xy=viewer.findChild<QGraphicsView*>("sliceView0");mode->setCurrentIndex(2);
        auto first=xy->mapFromScene(6.5*.5,(11-8-.5)*.7),last=xy->mapFromScene(1.5*.5,(11-2-.5)*.7);
        QTest::mousePress(xy->viewport(),Qt::LeftButton,Qt::NoModifier,first);QTest::mouseMove(xy->viewport(),last);QTest::mouseRelease(xy->viewport(),Qt::LeftButton,Qt::NoModifier,last);
        QCOMPARE(rois.count(),1);QCOMPARE(rois.last().first().toString(),QString("1,2,0,6,7,7"));
        viewer.setSelection(rois.last().first().toString(),"");auto* xz=viewer.findChild<QGraphicsView*>("sliceView1");
        first=xz->mapFromScene(2.5*.5,(7-1-.5)*2);last=xz->mapFromScene(5.5*.5,(7-4-.5)*2);
        QTest::mousePress(xz->viewport(),Qt::LeftButton,Qt::NoModifier,first);QTest::mouseRelease(xz->viewport(),Qt::LeftButton,Qt::NoModifier,last);
        QCOMPARE(rois.count(),2);QCOMPARE(rois.last().first().toString(),QString("2,2,1,4,7,4"));
        viewer.setPickingEnabled(false);mode->setCurrentIndex(1);QTest::mouseClick(xy->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(0,0));QCOMPARE(seeds.count(),3);
    }
    void windowAndMaskOverlay() {
        auto v=std::make_shared<medvessel::Volume3D<float>>(2,2,2);v->at(1,0,0)=100;
        auto mask=std::make_shared<medvessel::Volume3D<float>>(2,2,2);mask->at(1,0,0)=1;
        SliceViewer viewer;viewer.setVolume(v,"base");
        viewer.findChild<QDoubleSpinBox*>("windowWidth")->setValue(100);
        viewer.findChild<QDoubleSpinBox*>("windowLevel")->setValue(50);
        QCOMPARE(viewer.sliceImage(0,0).pixelColor(1,1),QColor(255,255,255));
        QString error;QVERIFY(viewer.setMask(mask,"mask",&error));
        viewer.findChild<QSlider*>("maskOpacity")->setValue(100);
        QCOMPARE(viewer.sliceImage(0,0).pixelColor(1,1),QColor(255,128,0));
        QCOMPARE(viewer.sliceImage(1,0).pixelColor(1,1),QColor(255,128,0));
        QCOMPARE(viewer.sliceImage(2,1).pixelColor(0,1),QColor(255,128,0));
        QCOMPARE(viewer.sliceImage(0,0).pixelColor(0,0),QColor(0,0,0));
        viewer.findChild<QCheckBox*>("showMask")->setChecked(false);
        QCOMPARE(viewer.sliceImage(0,0).pixelColor(1,1),QColor(255,255,255));
        viewer.findChild<QCheckBox*>("showMask")->setChecked(true);
        auto g=v->geometry();g.origin[0]=1;
        QVERIFY(!viewer.setMask(std::make_shared<medvessel::Volume3D<float>>(2,2,2,g),"shifted",&error));
        QCOMPARE(viewer.sliceImage(0,0).pixelColor(1,1),QColor(255,255,255));
        mask->at(0,0,0)=0.5;QVERIFY(!viewer.setMask(mask,"probability",&error));
        mask->at(0,0,0)=0;QVERIFY(viewer.setMask(mask,"mask"));viewer.setVolume(v,"new");
        QCOMPARE(viewer.sliceImage(0,0).pixelColor(1,1),QColor(255,255,255));
        QSignalSpy done(&viewer,&SliceViewer::maskLoaded);viewer.loadMask(temp_.path()+"/missing-mask.nii");QTRY_COMPARE(done.count(),1);QVERIFY(!done.first().first().toBool());
#ifdef MEDVESSEL_GUI_NIFTI
        auto path=temp_.path()+"/overlay.nii.gz";medvessel::writeNifti(*mask,path.toUtf8().toStdString());
        done.clear();viewer.loadMask(path);QTRY_COMPARE(done.count(),1);QVERIFY(done.first().first().toBool());
        QCOMPARE(viewer.sliceImage(0,0).pixelColor(1,1),QColor(255,128,0));
#endif
    }
    void sliceMappingAndLoading() {
        medvessel::ImageGeometry g;g.spacing={0.5,0.7,2};g.direction={-1,0,0,0,1,0,0,0,-1};
        auto volume=std::make_shared<medvessel::Volume3D<float>>(3,4,5,g);
        for(int z=0;z<5;++z)for(int y=0;y<4;++y)for(int x=0;x<3;++x)volume->at(x,y,z)=x+10*y+100*z;
        SliceViewer viewer;viewer.setVolume(volume,"test");
        auto xy=viewer.sliceImage(0,2);QCOMPARE(xy.size(),QSize(3,4));
        QCOMPARE(qGray(xy.pixel(2,0)),int(232./432*255));
        QCOMPARE(qGray(xy.pixel(0,3)),int(200./432*255));
        QCOMPARE(viewer.sliceImage(1,1).size(),QSize(3,5));
        QCOMPARE(viewer.sliceImage(2,2).size(),QSize(4,5));
        QVERIFY(viewer.sliceImage(0,5).isNull());
        viewer.findChild<QSlider*>("slice0")->setValue(4);
        QCOMPARE(viewer.findChild<QSlider*>("slice0")->value(),4);
        QSignalSpy loaded(&viewer,&SliceViewer::loaded);viewer.load(temp_.path()+"/missing.nii.gz");
        QTRY_COMPARE(loaded.count(),1);QVERIFY(!loaded.first().first().toBool());QVERIFY(viewer.sliceImage(0,0).isNull());
#ifdef MEDVESSEL_GUI_NIFTI
        if(!source_.isEmpty()){
            MainWindow window;window.setConfig(base());window.show();
            auto* panel=window.findChild<SliceViewer*>("inputSlices");QSignalSpy ready(panel,&SliceViewer::loaded);
            QTest::mouseClick(window.findChild<QPushButton*>("browseSlices"),Qt::LeftButton);
            QTRY_COMPARE(ready.count(),1);QVERIFY(ready.first().first().toBool());QVERIFY(!panel->sliceImage(0,0).isNull());
            auto* mode=panel->findChild<QComboBox*>("pickMode");mode->setCurrentIndex(1);auto* xy=panel->findChild<QGraphicsView*>("sliceView0");
            auto point=xy->mapFromScene(12.5*.5,(33-8-.5)*.5);QTest::mouseClick(xy->viewport(),Qt::LeftButton,Qt::NoModifier,point);
            QCOMPARE(window.config().seeds,QString("12,8,8"));QTest::mouseClick(xy->viewport(),Qt::LeftButton,Qt::NoModifier,point);QCOMPARE(window.config().seeds,QString("12,8,8"));
            mode->setCurrentIndex(2);auto start=xy->mapFromScene(3.5*.5,(33-5-.5)*.5),finish=xy->mapFromScene(15.5*.5,(33-20-.5)*.5);
            QTest::mousePress(xy->viewport(),Qt::LeftButton,Qt::NoModifier,start);QTest::mouseRelease(xy->viewport(),Qt::LeftButton,Qt::NoModifier,finish);
            QCOMPARE(window.config().roi,QString("3,5,0,13,16,17"));QVERIFY(window.config().seedsInSource);
            auto shot=qEnvironmentVariable("MEDVESSEL_SLICE_SCREENSHOT");
            if(!shot.isEmpty()){QTest::qWait(100);QVERIFY(window.grab().save(shot));}
        }
#endif
    }
    void initTestCase() {
        QApplication::setStyle("Fusion");
        QVERIFY(temp_.isValid());const auto bin=QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        cli_=bin+"/medvessel_cli"+suffix;
        if(!QFileInfo::exists(cli_))QSKIP("GUI-only build: pipeline executables unavailable");
        QProcess phantom;phantom.start(bin+"/medvessel_phantom"+suffix,{temp_.path()+"/input with spaces"});
        QVERIFY(phantom.waitForFinished(15000));QCOMPARE(phantom.exitCode(),0);
        source_=temp_.path()+"/input with spaces/phantom.nii.gz";QVERIFY(QFileInfo::exists(source_));
    }
    void roiConversionAndValidation() {
        JobConfig c;c.roi="40,55,0,100,100,50";c.seeds="89,87,19;90,88,20";
        auto args=c.arguments("output");auto idx=args.indexOf("--seed");QCOMPARE(args[idx+1],QString("49,32,19"));
        c.seedsInSource=false;c.seeds="49,32,19";QCOMPARE(c.arguments("o")[idx+1],QString("49,32,19"));
        c.seeds="100,32,19";QVERIFY_EXCEPTION_THROWN(c.arguments("o"),std::exception);
        c.seeds.clear();c.low=.8;c.high=.2;QVERIFY_EXCEPTION_THROWN(c.arguments("o"),std::exception);
    }
    void configurationRoundTrip() {
        auto c=base();c.roi="1,2,3,20,20,20";c.seeds="5,6,7";c.minIntensity="100";c.maxIntensity="900";c.nms=true;c.prune=1.2;
        QString error;auto path=temp_.path()+"/config.json";QVERIFY2(writeJson(path,c.toJson(),&error),qPrintable(error));
        auto restored=JobConfig::fromJson(readJson(path));QCOMPARE(restored.arguments("o"),c.arguments("o"));QCOMPARE(restored.toJson(),c.toJson());
        QVERIFY_EXCEPTION_THROWN(JobConfig::fromJson({{"schema_version",99}}),std::exception);
    }
    void fullWindowPipelineAndReopen() {
        if(source_.isEmpty())QSKIP("Pipeline executables unavailable");
        MainWindow window;window.setConfig(base());window.show();
        QSignalSpy success(window.job(),&AnalysisJob::succeeded),failure(window.job(),&AnalysisJob::failed),stages(window.job(),&AnalysisJob::stageChanged);
        QString error;QVERIFY2(window.startAnalysis(&error),qPrintable(error));QVERIFY(!window.findChild<QPushButton*>("run")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(success.count()+failure.count()>0,30000);
        QVERIFY2(failure.isEmpty(),failure.isEmpty()?"":qPrintable(failure.first().first().toString()));QCOMPARE(success.count(),1);QVERIFY(stages.count()>=8);
        completed_=window.job()->outputDirectory();QCOMPARE(readJson(completed_+"/gui_run.json")["status"].toString(),QString("completed"));
        QVERIFY(window.findChild<QPushButton*>("run")->isEnabled());auto* table=window.findChild<QTableView*>("branches");QVERIFY(table->model()->rowCount()>0);
        QVERIFY2(window.openResult(completed_,&error),qPrintable(error));
        MainWindow reopened;QVERIFY2(reopened.openResult(completed_,&error),qPrintable(error));QCOMPARE(reopened.findChild<QTableView*>("branches")->model()->rowCount(),table->model()->rowCount());
        auto shot=qEnvironmentVariable("MEDVESSEL_GUI_SCREENSHOT");if(!shot.isEmpty()){window.resize(1480,940);QTest::qWait(100);QVERIFY(window.grab().save(shot));}
    }
    void rejectMissingInputWithoutOutput() {
        AnalysisJob job;auto c=base();c.input=temp_.path()+"/missing.nii.gz";QString error;
        QVERIFY(!job.start(c,&error));QVERIFY(!error.isEmpty());QVERIFY(!job.running());
    }
    void unicodePaths() {
        if(source_.isEmpty())QSKIP("Pipeline executables unavailable");
        auto c=base();c.input=temp_.path()+"/中文影像.nii.gz";c.outputRoot=temp_.path()+"/中文结果";QVERIFY(QFile::copy(source_,c.input));
        AnalysisJob job;QSignalSpy success(&job,&AnalysisJob::succeeded),failure(&job,&AnalysisJob::failed);QString error;
        QVERIFY2(job.start(c,&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(success.count()+failure.count()>0,15000);
        QVERIFY2(failure.isEmpty(),failure.isEmpty()?"":qPrintable(failure.first().first().toString()));QCOMPARE(success.count(),1);
        QCOMPARE(QDir::fromNativeSeparators(readJson(job.outputDirectory()+"/run.json")["input_path"].toString()),QDir::fromNativeSeparators(c.input));
    }
    void invalidInputFailsAndCannotReopen() {
        if(source_.isEmpty())QSKIP("Pipeline executables unavailable");
        QFile broken(temp_.path()+"/broken.nii");QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("invalid");broken.close();
        AnalysisJob job;auto c=base();c.input=broken.fileName();QSignalSpy failure(&job,&AnalysisJob::failed),success(&job,&AnalysisJob::succeeded);QString error;
        QVERIFY2(job.start(c,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(failure.count(),1,10000);QVERIFY(success.isEmpty());
        QCOMPARE(readJson(job.outputDirectory()+"/gui_run.json")["status"].toString(),QString("failed"));
        MainWindow window;QVERIFY(!window.openResult(job.outputDirectory(),&error));
    }
    void cancellationAndFreshRunDirectory() {
        if(source_.isEmpty())QSKIP("Pipeline executables unavailable");
        AnalysisJob job;auto c=base();c.scales="0.5,1,1.5,2,2.5,3";QSignalSpy canceled(&job,&AnalysisJob::canceled),success(&job,&AnalysisJob::succeeded);QString error;
        QVERIFY2(job.start(c,&error),qPrintable(error));job.cancel();QTRY_COMPARE_WITH_TIMEOUT(canceled.count(),1,10000);QVERIFY(success.isEmpty());
        QCOMPARE(readJson(job.outputDirectory()+"/gui_run.json")["status"].toString(),QString("canceled"));QVERIFY(job.outputDirectory()!=completed_);
        QVERIFY(QFileInfo::exists(completed_+"/mask.nii.gz"));MainWindow window;QVERIFY(!window.openResult(job.outputDirectory(),&error));
    }
    void failedExecutableDoesNotLeaveRunningState() {
        if(source_.isEmpty())QSKIP("Pipeline executables unavailable");
        const auto path=temp_.path()+"/missing-at-start.exe";QVERIFY(QFile::copy(cli_,path));
        AnalysisJob job;auto c=base();c.executable=path;QSignalSpy failure(&job,&AnalysisJob::failed);QString error;
        // Deterministic launch race: executable disappears after validation but before start.
        // A malformed .exe can trigger an OS loader dialog under CTest on Windows.
        connect(&job,&AnalysisJob::runningChanged,&job,[path](bool running){if(running)QFile::remove(path);});
        QVERIFY2(job.start(c,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(failure.count(),1,10000);QVERIFY(!job.running());
        QCOMPARE(readJson(job.outputDirectory()+"/gui_run.json")["status"].toString(),QString("failed"));
    }
};
int main(int argc,char** argv) {
#ifdef MEDVESSEL_GUI_VTK
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif
    QApplication app(argc,argv);GuiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "gui_tests.moc"

#include "MainWindow.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QTimer>
#include <QMessageBox>
#ifdef MEDVESSEL_GUI_VTK
#include "ThreeDViewer.hpp"
#include "SliceViewer.hpp"
#include <QVTKOpenGLNativeWidget.h>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTableView>
#endif

int main(int argc,char** argv) {
#ifdef MEDVESSEL_GUI_VTK
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
#endif
    QApplication app(argc,argv);app.setStyle("Fusion");app.setOrganizationName("MEDVessel");app.setApplicationName("MEDVessel");
    QCommandLineParser parser;parser.setApplicationDescription("MEDVessel Qt 6 血管分析工作台");parser.addHelpOption();
    parser.addOption({"result","打开已有结果目录","directory"});parser.addOption({"config","载入 GUI 配置","file"});
    parser.addOption({"capture","保存窗口 PNG 后退出；需要 --result","png"});
#ifdef MEDVESSEL_GUI_VTK
    parser.addOption({"three-d","打开并加载三维结果；需要 --result"});
    parser.addOption({"result-slices","将首个分支定位到结果切片；需要 --result"});
#endif
    parser.process(app);
    if(parser.isSet("capture")&&!parser.isSet("result"))return 2;
#ifdef MEDVESSEL_GUI_VTK
    if(parser.isSet("three-d")&&!parser.isSet("result"))return 2;
    if(parser.isSet("result-slices")&&!parser.isSet("result"))return 2;
#endif
    medvessel::desktop::MainWindow window;QSettings settings;window.restoreGeometry(settings.value("geometry").toByteArray());window.show();
    QTimer::singleShot(0,&window,[&]{
        try{if(parser.isSet("config"))window.setConfig(medvessel::desktop::JobConfig::fromJson(medvessel::desktop::readJson(parser.value("config"))));
            if(parser.isSet("result")){QString error;if(!window.openResult(parser.value("result"),&error))throw std::runtime_error(error.toStdString());}
#ifdef MEDVESSEL_GUI_VTK
            if(parser.isSet("three-d")||parser.isSet("result-slices")){
                auto* viewer=window.findChild<medvessel::desktop::ThreeDViewer*>();auto* tabs=qobject_cast<QTabWidget*>(viewer->parentWidget()->parentWidget());if(tabs)tabs->setCurrentWidget(viewer);
                QObject::connect(viewer,&medvessel::desktop::ThreeDViewer::loaded,&window,[&,viewer](bool ok){if(!ok){if(parser.isSet("capture"))app.exit(2);return;}window.findChild<QTableView*>("branches")->selectRow(0);
                    if(parser.isSet("result-slices")){if(viewer->branchCount()==0){if(parser.isSet("capture"))app.exit(2);return;}auto* slices=window.findChild<medvessel::desktop::SliceViewer*>("resultSlices");QObject::connect(slices,&medvessel::desktop::SliceViewer::loaded,&window,[&](bool success){if(!success){if(parser.isSet("capture"))app.exit(2);return;}if(parser.isSet("capture"))QTimer::singleShot(300,&window,[&]{app.exit(window.grab().save(parser.value("capture"))?0:1);});});viewer->locateSlicesRequested();return;}
                    if(parser.isSet("capture"))QTimer::singleShot(700,&window,[&]{app.exit(window.grab().save(parser.value("capture"))?0:1);});});
                viewer->load();return;
            }
#endif
            if(parser.isSet("capture"))QTimer::singleShot(250,&window,[&]{app.exit(window.grab().save(parser.value("capture"))?0:1);});
        }catch(const std::exception& e){if(parser.isSet("capture"))app.exit(2);else QMessageBox::warning(&window,"无法打开",QString::fromUtf8(e.what()));}
    });
    return app.exec();
}

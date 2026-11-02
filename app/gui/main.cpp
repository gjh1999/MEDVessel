#include "MainWindow.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QTimer>
#include <QMessageBox>

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setStyle("Fusion");app.setOrganizationName("MEDVessel");app.setApplicationName("MEDVessel");
    QCommandLineParser parser;parser.setApplicationDescription("MEDVessel Qt 6 血管分析工作台");parser.addHelpOption();
    parser.addOption({"result","打开已有结果目录","directory"});parser.addOption({"config","载入 GUI 配置","file"});
    parser.addOption({"capture","保存窗口 PNG 后退出；需要 --result","png"});parser.process(app);
    if(parser.isSet("capture")&&!parser.isSet("result"))return 2;
    medvessel::desktop::MainWindow window;QSettings settings;window.restoreGeometry(settings.value("geometry").toByteArray());window.show();
    QTimer::singleShot(0,&window,[&]{
        try{if(parser.isSet("config"))window.setConfig(medvessel::desktop::JobConfig::fromJson(medvessel::desktop::readJson(parser.value("config"))));
            if(parser.isSet("result")){QString error;if(!window.openResult(parser.value("result"),&error))throw std::runtime_error(error.toStdString());}
            if(parser.isSet("capture"))QTimer::singleShot(250,&window,[&]{app.exit(window.grab().save(parser.value("capture"))?0:1);});
        }catch(const std::exception& e){if(parser.isSet("capture"))app.exit(2);else QMessageBox::warning(&window,"无法打开",QString::fromUtf8(e.what()));}
    });
    return app.exec();
}

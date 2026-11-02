#include "MainWindow.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTableView>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QJsonArray>

using namespace medvessel::desktop;
class GuiTests:public QObject {
    Q_OBJECT
    QTemporaryDir temp_;
    QString source_,cli_,completed_;
    JobConfig base() const {JobConfig c;c.input=source_;c.outputRoot=temp_.path()+"/results with spaces";c.executable=cli_;c.threads=2;return c;}
private slots:
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
QTEST_MAIN(GuiTests)
#include "gui_tests.moc"

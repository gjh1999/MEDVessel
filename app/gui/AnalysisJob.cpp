#include "AnalysisJob.hpp"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QMap>

namespace medvessel::desktop {
AnalysisJob::AnalysisJob(QObject* parent):QObject(parent) {
    killTimer_.setSingleShot(true);killTimer_.setInterval(1500);
    connect(&killTimer_,&QTimer::timeout,this,[this]{if(active_)process_.kill();});
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{consume(outBuffer_,process_.readAllStandardOutput());});
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{consume(errBuffer_,process_.readAllStandardError());});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){
        if(e==QProcess::FailedToStart)complete("failed",process_.errorString());
    });
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        consume(outBuffer_,process_.readAllStandardOutput(),true);consume(errBuffer_,process_.readAllStandardError(),true);
        if(cancelRequested_){complete("canceled");return;}
        if(status!=QProcess::NormalExit||code!=0){complete("failed",QString("算法进程退出，代码 %1；请查看日志。").arg(code));return;}
        try {
            const auto metadata=readJson(QDir(output_).filePath("run.json"));
            if(metadata["schema_version"].toInt()!=1||!metadata["branches"].isDouble())throw std::runtime_error("run.json 不完整");
            for(const char* name:{"branches.csv","preview.svg","mask.nii.gz","centerline.csv"})
                if(!QFileInfo::exists(QDir(output_).filePath(name)))throw std::runtime_error("算法输出缺失");
            complete("completed");
        }catch(const std::exception& e){complete("failed",QString::fromUtf8(e.what()));}
    });
}
AnalysisJob::~AnalysisJob() {
    if(active_){process_.kill();process_.waitForFinished(3000);if(active_)complete("canceled");}
}
bool AnalysisJob::start(const JobConfig& config,QString* error) {
    auto fail=[&](const QString& message){if(error)*error=message;return false;};
    if(active_)return fail("已有任务正在运行");
    if(!QFileInfo::exists(config.input))return fail("请选择存在的 NIfTI 文件或 DICOM 目录");
    if(!QFileInfo(config.executable).isFile()||!QFileInfo(config.executable).isExecutable())return fail("找不到可执行的 medvessel_cli，请在高级设置中选择");
    if(config.outputRoot.trimmed().isEmpty())return fail("请选择结果保存目录");
    QStringList args;
    try{config.arguments("validate");}catch(const std::exception& e){return fail(QString::fromUtf8(e.what()));}
    output_=QDir(config.outputRoot).absoluteFilePath("run-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8));
    if(!QDir().mkpath(output_))return fail("无法创建结果目录："+output_);
    args=config.arguments(output_);
    manifest_={{"schema_version",1},{"status","running"},{"started_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"config",config.toJson()},{"arguments",QJsonArray::fromStringList(args)}};
    QString ioError;
    if(!writeJson(QDir(output_).filePath("gui_run.json"),manifest_,&ioError))return fail("无法保存任务记录："+ioError);
    log_.setFileName(QDir(output_).filePath("process.log"));
    if(!log_.open(QIODevice::WriteOnly|QIODevice::Truncate)) {
        manifest_["status"]="failed";manifest_["error"]="无法创建运行日志";
        writeJson(QDir(output_).filePath("gui_run.json"),manifest_);
        return fail("无法创建运行日志");
    }
    cancelRequested_=false;outBuffer_.clear();errBuffer_.clear();active_=true;
    auto env=QProcessEnvironment::systemEnvironment();env.insert("OMP_NUM_THREADS",QString::number(config.threads));
    process_.setProcessEnvironment(env);process_.setWorkingDirectory(QFileInfo(config.executable).absolutePath());
    emit runningChanged(true);emit stageChanged("正在启动算法");
    process_.start(config.executable,args);return true;
}
void AnalysisJob::cancel() {
    if(!active_||cancelRequested_)return;
    cancelRequested_=true;emit stageChanged("正在停止…");process_.terminate();killTimer_.start();
}
void AnalysisJob::consume(QByteArray& buffer,const QByteArray& bytes,bool flush) {
    if(log_.isOpen()){log_.write(bytes);log_.flush();}buffer+=bytes;
    while(buffer.contains('\n')||(flush&&!buffer.isEmpty())) {
        auto pos=buffer.indexOf('\n');if(pos<0)pos=buffer.size();
        auto line=QString::fromUtf8(buffer.left(pos)).trimmed();buffer.remove(0,pos+1);
        if(line.startsWith("MEDVESSEL_STAGE\t")) {
            const auto code=line.section('\t',1);
            static const QMap<QString,QString> names{{"read","读取影像 / 裁剪 ROI"},{"enhance","多尺度 Frangi 增强"},
                {"nms","筛选 NMS 种子"},{"segment","分割与连通域过滤"},{"distance","计算 EDT 距离场"},
                {"skeleton","提取骨架 / 修剪短枝"},{"graph","构建分支图"},{"reports","生成测量报告与投影"},{"write","保存影像与元数据"}};
            if(!cancelRequested_)emit stageChanged(names.value(code,code));
        }
        if(!line.isEmpty())emit logMessage(line);
    }
}
void AnalysisJob::complete(const QString& status,const QString& error) {
    if(!active_)return;
    killTimer_.stop();manifest_["status"]=status;manifest_["finished_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    manifest_["exit_code"]=process_.exitCode();if(!error.isEmpty())manifest_["error"]=error;
    QString saveError;bool saved=writeJson(QDir(output_).filePath("gui_run.json"),manifest_,&saveError);
    log_.close();active_=false;emit runningChanged(false);
    if(!saved){emit failed("无法保存最终任务状态："+saveError);return;}
    if(status=="completed")emit succeeded(output_);
    else if(status=="canceled")emit canceled();else emit failed(error);
}
}

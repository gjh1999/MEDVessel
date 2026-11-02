#pragma once
#include "JobConfig.hpp"
#include <QProcess>
#include <QFile>
#include <QTimer>

namespace medvessel::desktop {
class AnalysisJob:public QObject {
    Q_OBJECT
public:
    explicit AnalysisJob(QObject* parent=nullptr);
    ~AnalysisJob() override;
    bool start(const JobConfig& config,QString* error=nullptr);
    void cancel();
    bool running() const {return active_;}
    QString outputDirectory() const {return output_;}
signals:
    void runningChanged(bool running);
    void stageChanged(const QString& stage);
    void logMessage(const QString& line);
    void succeeded(const QString& directory);
    void failed(const QString& error);
    void canceled();
private:
    QProcess process_;
    QTimer killTimer_;
    QFile log_;
    QByteArray outBuffer_,errBuffer_;
    QString output_;
    QJsonObject manifest_;
    bool active_=false,cancelRequested_=false;
    void consume(QByteArray& buffer,const QByteArray& bytes,bool flush=false);
    void complete(const QString& status,const QString& error={});
};
}

#pragma once
#include <QJsonObject>
#include <QStringList>

namespace medvessel::desktop {
struct JobConfig {
    QString input, outputRoot, executable;
    QString scales="0.5,1,2", roi, seeds, minIntensity, maxIntensity;
    double alpha=.5, beta=.5, c=100, low=.1, high=.3, nmsStep=1, prune=0;
    int connectivity=26, minComponent=0, threads=4;
    bool dark=false, nms=false, seedsInSource=true;
    QStringList arguments(const QString& output) const;
    QJsonObject toJson() const;
    static JobConfig fromJson(const QJsonObject& object);
};
bool writeJson(const QString& path,const QJsonObject& object,QString* error=nullptr);
QJsonObject readJson(const QString& path);
}

#include "JobConfig.hpp"
#include "io/RunOptions.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRegularExpression>
#include <stdexcept>

namespace medvessel::desktop {
QStringList JobConfig::arguments(const QString& output) const {
    QStringList args{input,output,"--scales",scales.trimmed(),"--alpha",QString::number(alpha,'g',16),
        "--beta",QString::number(beta,'g',16),"--c",QString::number(c,'g',16),
        "--low",QString::number(low,'g',16),"--high",QString::number(high,'g',16),
        "--connectivity",QString::number(connectivity),"--min-component",QString::number(minComponent),
        "--prune-mm",QString::number(prune,'g',16),"--nms-step",QString::number(nmsStep,'g',16)};
    args<<"--growth-band-mm"<<QString::number(growthBandMm,'g',16)
        <<"--seed-core-ratio"<<QString::number(seedCoreRatio,'g',16);
    std::optional<std::array<std::size_t,6>> crop;
    if(!roi.trimmed().isEmpty()) {
        crop=medvessel::parseIndices<6>(roi.trimmed().toStdString());
        if(!(*crop)[3]||!(*crop)[4]||!(*crop)[5]) throw std::invalid_argument("ROI 宽、高、深必须大于 0");
        args<<"--roi"<<roi.trimmed();
    }
    for(const auto& seed:seeds.split(QRegularExpression("[;\\n]+"),Qt::SkipEmptyParts)) {
        auto p=medvessel::parseIndices<3>(seed.trimmed().toStdString());
        if(crop) for(int i=0;i<3;++i) {
            if(seedsInSource) {
                if(p[i]<(*crop)[i]) throw std::invalid_argument("种子在 ROI 之外，请检查原图索引");
                p[i]-=(*crop)[i];
            }
            if(p[i]>=(*crop)[i+3]) throw std::invalid_argument("种子在 ROI 之外");
        }
        args<<"--seed"<<QString("%1,%2,%3").arg(qulonglong(p[0])).arg(qulonglong(p[1])).arg(qulonglong(p[2]));
    }
    if(!minIntensity.trimmed().isEmpty()) args<<"--min-intensity"<<minIntensity.trimmed();
    if(!maxIntensity.trimmed().isEmpty()) args<<"--max-intensity"<<maxIntensity.trimmed();
    if(dark) args<<"--dark";
    if(nms) args<<"--nms-seeds";
    std::vector<std::string> options;
    for(int i=2;i<args.size();++i) options.push_back(args[i].toStdString());
    medvessel::parseRunOptions(options);
    if(threads<1||threads>256) throw std::invalid_argument("线程数必须为 1–256");
    return args;
}
QJsonObject JobConfig::toJson() const {
    return {{"schema_version",1},{"input",input},{"output_root",outputRoot},{"executable",executable},
        {"scales",scales},{"roi",roi},{"seeds",seeds},{"seeds_in_source",seedsInSource},
        {"min_intensity",minIntensity},{"max_intensity",maxIntensity},{"alpha",alpha},{"beta",beta},
        {"c",c},{"low",low},{"high",high},{"nms_step",nmsStep},{"prune",prune},
        {"growth_band_mm",growthBandMm},{"seed_core_ratio",seedCoreRatio},
        {"connectivity",connectivity},{"min_component",minComponent},{"threads",threads},{"dark",dark},{"nms",nms}};
}
JobConfig JobConfig::fromJson(const QJsonObject& o) {
    if(o.value("schema_version").toInt()!=1||!o.value("scales").isString())
        throw std::invalid_argument("不是支持的 MEDVessel GUI 配置文件（schema_version=1）");
    JobConfig c;
    c.input=o["input"].toString();c.outputRoot=o["output_root"].toString();c.executable=o["executable"].toString();
    c.scales=o["scales"].toString();c.roi=o["roi"].toString();c.seeds=o["seeds"].toString();
    c.seedsInSource=o["seeds_in_source"].toBool(true);
    c.minIntensity=o["min_intensity"].toString();c.maxIntensity=o["max_intensity"].toString();
    c.alpha=o["alpha"].toDouble(.5);c.beta=o["beta"].toDouble(.5);c.c=o["c"].toDouble(100);
    c.low=o["low"].toDouble(.1);c.high=o["high"].toDouble(.3);c.nmsStep=o["nms_step"].toDouble(1);
    c.growthBandMm=o["growth_band_mm"].toDouble(0);c.seedCoreRatio=o["seed_core_ratio"].toDouble(.35);
    c.prune=o["prune"].toDouble();c.connectivity=o["connectivity"].toInt(26);
    c.minComponent=o["min_component"].toInt();c.threads=o["threads"].toInt(4);
    c.dark=o["dark"].toBool();c.nms=o["nms"].toBool();c.arguments("validation-only");return c;
}
bool writeJson(const QString& path,const QJsonObject& o,QString* error) {
    QSaveFile file(path);const auto bytes=QJsonDocument(o).toJson(QJsonDocument::Indented);
    bool ok=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
    if(!ok&&error)*error=file.errorString();
    return ok;
}
QJsonObject readJson(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(("无法读取："+path).toStdString());
    QJsonParseError error;auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject())throw std::runtime_error(("JSON 格式无效："+path).toStdString());
    return doc.object();
}
}

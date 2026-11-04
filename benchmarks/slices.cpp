#include "SliceViewer.hpp"
#include <QApplication>
#include <QComboBox>
#include <QSlider>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

using namespace medvessel;
using namespace medvessel::desktop;
namespace {
QJsonObject memory() {
    QJsonObject result;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))){
        result["working_set_mib"]=double(counters.WorkingSetSize)/(1024*1024);
        result["peak_working_set_mib"]=double(counters.PeakWorkingSetSize)/(1024*1024);
        result["private_commit_mib"]=double(counters.PrivateUsage)/(1024*1024);
    }
#endif
    return result;
}
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    application.setStyle("Fusion");
    QJsonObject report{{"data","synthetic; not a real CTA performance claim"},{"dimensions",QJsonArray{512,512,275}},{"scope","CPU sampling and Qt scene updates; no disk I/O or GPU presentation timing"}};
    QElapsedTimer timer;timer.start();
    ImageGeometry geometry;geometry.spacing={.4,.4,.6};double angle=.5235987755982988;
    geometry.direction={std::cos(angle),-std::sin(angle),0,std::sin(angle),std::cos(angle),0,0,0,1};
    auto volume=std::make_shared<SliceViewer::Volume>(512,512,275,geometry);
    auto mask=std::make_shared<SliceViewer::Volume>(512,512,275,geometry);
    for(int z=0;z<275;++z)for(int y=0;y<512;++y)for(int x=0;x<512;++x){volume->at(x,y,z)=float(x+2*y+3*z);mask->at(x,y,z)=((x-256)*(x-256)+(y-256)*(y-256)<400)?1.f:0.f;}
    report["synthetic_prepare_ms"]=timer.nsecsElapsed()/1e6;
    SliceViewer viewer;viewer.resize(900,1100);
    timer.restart();viewer.setVolume(volume,"synthetic-512x512x275");report["set_volume_and_native_planes_ms"]=timer.nsecsElapsed()/1e6;
    viewer.setMask(mask,"synthetic-mask");report["native_memory"]=memory();
    timer.restart();viewer.findChild<QComboBox*>("sliceOrientation")->setCurrentIndex(1);report["anatomical_initial_three_planes_ms"]=timer.nsecsElapsed()/1e6;
    report["anatomical_memory"]=memory();
    QJsonArray timings;
    auto* slider=viewer.findChild<QSlider*>("slice0");
    for(int i=0;i<20;++i){timer.restart();slider->setValue((i*19)%std::max(1,slider->maximum()));timings.append(timer.nsecsElapsed()/1e6);}
    report["axial_scroll_20_ms"]=timings;
    timer.restart();for(int i=0;i<1000;++i){auto image=viewer.sliceImage(0,slider->value());if(image.isNull())return 2;}
    report["cached_slice_1000_requests_ms"]=timer.nsecsElapsed()/1e6;
    report["final_memory"]=memory();
    std::cout<<QJsonDocument(report).toJson(QJsonDocument::Indented).toStdString();
}

#pragma once
#include "skeleton/CenterlineGraph.hpp"
#include <fstream>
#include <filesystem>

namespace medvessel {
inline void writePreviewSvg(const Volume3D<float>& source,const Volume3D<float>& vesselness,
    const Volume3D<std::uint8_t>& mask,const CenterlineGraph& graph,const std::filesystem::path& path) {
    const auto d=source.dimensions();
    std::array<std::size_t,3> axes{0,1,2};
    std::stable_sort(axes.begin(),axes.end(),[&](std::size_t a,std::size_t b) {
        return d[a]*source.geometry().spacing[a]>d[b]*source.geometry().spacing[b];
    });
    const auto horizontal=std::min(axes[0],axes[1]),vertical=std::max(axes[0],axes[1]);
    const auto width=std::min<std::size_t>(d[horizontal],128),height=std::min<std::size_t>(d[vertical],128);
    std::array<std::vector<double>,3> projection;
    for(auto& p:projection) p.assign(width*height,-std::numeric_limits<double>::infinity());
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x) {
        const std::array<std::size_t,3> p{x,y,z};
        const auto index=(p[vertical]*height/d[vertical])*width+p[horizontal]*width/d[horizontal];
        projection[0][index]=std::max(projection[0][index],static_cast<double>(source.at(x,y,z)));
        projection[1][index]=std::max(projection[1][index],static_cast<double>(vesselness.at(x,y,z)));
        projection[2][index]=std::max(projection[2][index],static_cast<double>(mask.at(x,y,z)!=0));
    }
    auto extentX=d[horizontal]*source.geometry().spacing[horizontal],extentZ=d[vertical]*source.geometry().spacing[vertical];
    double scale=220/std::max(extentX,extentZ),plotW=extentX*scale,plotH=extentZ*scale;
    std::ofstream out(path);
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1080\" height=\"340\" viewBox=\"0 0 1080 340\">"
       <<"<rect width=\"1080\" height=\"340\" fill=\"#101b2c\"/>"
       <<"<g font-family=\"Arial,sans-serif\" fill=\"#eef5ff\">"
       <<"<text x=\"24\" y=\"30\" font-size=\"18\">MEDVessel | "<<"XYZ"[horizontal]<<"XYZ"[vertical]<<" maximum projections</text>";
    const char* labels[]={"Source intensity","Vesselness","Segmentation","Centreline branches"};
    for(int panel=0;panel<4;++panel) {
        double left=24+264*panel,top=70;
        out<<"<text x=\""<<left<<"\" y=\"57\" font-size=\"13\">"<<labels[panel]<<"</text>"
           <<"<rect x=\""<<left<<"\" y=\""<<top<<"\" width=\""<<plotW<<"\" height=\""<<plotH<<"\" fill=\"#02060c\"/>";
        if(panel<3) {
            double minimum=panel==0?*std::min_element(projection[0].begin(),projection[0].end()):0;
            double maximum=panel==0?*std::max_element(projection[0].begin(),projection[0].end()):1;
            for(std::size_t z=0;z<height;++z) for(std::size_t x=0;x<width;++x) {
                double value=std::clamp((projection[panel][z*width+x]-minimum)/(maximum>minimum?maximum-minimum:1),0.0,1.0);
                int colour=static_cast<int>(value*255);
                out<<"<rect x=\""<<left+x*plotW/width<<"\" y=\""<<top+z*plotH/height
                   <<"\" width=\""<<plotW/width+0.02<<"\" height=\""<<plotH/height+0.02
                   <<"\" fill=\"rgb("<<colour<<','<<colour<<','<<colour<<")\"/>";
            }
        } else {
            const char* palette[]={"#77c8ff","#73e0ae","#ffb86b","#dd9cff","#ff819e"};
            for(std::size_t branch=0;branch<graph.branches.size();++branch) {
                out<<"<polyline fill=\"none\" stroke-width=\"2\" stroke=\""<<palette[branch%5]<<"\" points=\"";
                for(auto world:graph.branches[branch].centerlineWorld) {
                    auto p=source.geometry().worldToIndex(world);
                    out<<left+(p[horizontal]+0.5)*plotW/d[horizontal]<<','<<top+(p[vertical]+0.5)*plotH/d[vertical]<<' ';
                }
                out<<"\"/>";
            }
        }
    }
    out<<"<text x=\"24\" y=\"318\" font-size=\"11\" fill=\"#b9c8df\">"
       <<"Image-axis projection; physical aspect ratio retained. Overlapping structures may hide topology errors."
       <<"</text></g></svg>";
    out.close();if(!out) throw std::runtime_error("Cannot write SVG preview");
}
}

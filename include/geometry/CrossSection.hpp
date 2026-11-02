#pragma once
#include "geometry/BranchAnalysis.hpp"
#include <cstdint>
#include <queue>

namespace medvessel {
struct CrossSectionResult {double areaMm2=0,equivalentDiameterMm=0;std::size_t foregroundSamples=0;};
// Nearest-neighbour mask sampling on a tangent-normal plane. The finite square
// must cover the entire lumen; a foreground perimeter signals truncation.
inline CrossSectionResult crossSection(const Volume3D<std::uint8_t>& mask,
    Point3D centre,Point3D tangent,double pixelMm,std::size_t halfWidth) {
    if(!std::isfinite(pixelMm)||pixelMm<=0||halfWidth==0||halfWidth>10000)
        throw std::invalid_argument("Invalid cross-section sampling grid");
    for(double v:centre) if(!std::isfinite(v)) throw std::invalid_argument("Invalid section centre");
    double magnitude=norm(tangent);
    if(!std::isfinite(magnitude)||magnitude<=0) throw std::invalid_argument("Invalid tangent");
    for(auto& v:tangent) v/=magnitude;
    int least=0;for(int i=1;i<3;++i) if(std::abs(tangent[i])<std::abs(tangent[least])) least=i;
    Point3D reference{};reference[least]=1;
    auto cross=[](Point3D a,Point3D b)->Point3D {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    auto u=cross(tangent,reference);double unorm=norm(u);for(auto& v:u) v/=unorm;
    auto v=cross(tangent,u);auto d=mask.dimensions();CrossSectionResult result;
    auto radius=static_cast<int>(halfWidth);
    const auto side=2*halfWidth+1;
    std::vector<std::uint8_t> sampled(side*side);
    for(int j=-radius;j<=radius;++j) for(int i=-radius;i<=radius;++i) {
        Point3D world{};for(int a=0;a<3;++a) world[a]=centre[a]+pixelMm*(i*u[a]+j*v[a]);
        auto p=mask.geometry().worldToIndex(world);std::array<std::size_t,3> q{};bool inside=true;
        for(int a=0;a<3;++a) {
            double rounded=std::floor(p[a]+0.5);
            if(!std::isfinite(rounded)||rounded<0||rounded>=static_cast<double>(d[a])) {inside=false;break;}
            q[a]=static_cast<std::size_t>(rounded);
        }
        if(inside&&mask.at(q[0],q[1],q[2])) sampled[(j+radius)*side+i+radius]=1;
    }
    // Only measure the lumen containing the centre, excluding nearby vessels.
    const auto centreIndex=halfWidth*side+halfWidth;
    if(sampled[centreIndex]) {
        std::queue<std::size_t> pending;pending.push(centreIndex);sampled[centreIndex]=2;
        while(!pending.empty()) {
            auto p=pending.front();pending.pop();auto x=p%side,y=p/side;
            if(x==0||y==0||x==side-1||y==side-1)
                throw std::invalid_argument("Cross-section window truncates foreground");
            ++result.foregroundSamples;
            for(auto q:{p-1,p+1,p-side,p+side}) if(sampled[q]==1) {sampled[q]=2;pending.push(q);}
        }
    }
    result.areaMm2=result.foregroundSamples*pixelMm*pixelMm;
    result.equivalentDiameterMm=2*std::sqrt(result.areaMm2/std::acos(-1.0));
    return result;
}
// Circumcircle curvature on a polyline after a specified number of neighbour
// averaging passes. Endpoints are undefined and represented by NaN.
inline std::vector<double> polylineCurvature(const std::vector<Point3D>& points,
                                           std::size_t smoothingPasses=0) {
    polylineLengthMm(points);auto smoothed=points;
    for(std::size_t pass=0;pass<smoothingPasses;++pass) {
        auto previous=smoothed;
        for(std::size_t i=1;i+1<points.size();++i) for(int a=0;a<3;++a)
            smoothed[i][a]=(previous[i-1][a]+2*previous[i][a]+previous[i+1][a])/4;
    }
    std::vector<double> result(points.size(),std::numeric_limits<double>::quiet_NaN());
    for(std::size_t i=1;i+1<points.size();++i) {
        auto a=subtract(smoothed[i],smoothed[i-1]),b=subtract(smoothed[i+1],smoothed[i]);
        auto chord=subtract(smoothed[i+1],smoothed[i-1]);
        double denominator=norm(a)*norm(b)*norm(chord);
        Point3D cross{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
        if(denominator>0) result[i]=2*norm(cross)/denominator;
    }
    return result;
}
} // namespace medvessel

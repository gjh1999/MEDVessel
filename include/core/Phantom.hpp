#pragma once
#include "geometry/BranchAnalysis.hpp"
#include <random>

namespace medvessel {
struct PhantomResult {Volume3D<float> source,reference;};
// Piecewise-linear centreline union with a Gaussian radial intensity profile.
inline PhantomResult tubePhantom(std::array<std::size_t,3> d,const ImageGeometry& geometry,
    const std::vector<std::vector<Point3D>>& branches,double radiusMm=2,double noiseStddev=0,
    std::uint32_t seed=42) {
    if(branches.empty()||!std::isfinite(radiusMm)||radiusMm<=0||
       !std::isfinite(noiseStddev)||noiseStddev<0) throw std::invalid_argument("Invalid phantom parameters");
    for(const auto& b:branches) if(b.size()<2||polylineLengthMm(b)==0)
        throw std::invalid_argument("Phantom branch requires a nonzero segment");
    PhantomResult result{Volume3D<float>(d[0],d[1],d[2],geometry),Volume3D<float>(d[0],d[1],d[2],geometry)};
    std::mt19937 random(seed);std::normal_distribution<double> noise(0,noiseStddev>0?noiseStddev:1);
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x) {
        auto world=geometry.indexToWorld({static_cast<double>(x),static_cast<double>(y),static_cast<double>(z)});
        double distance=std::numeric_limits<double>::infinity();
        for(const auto& branch:branches) for(std::size_t i=1;i<branch.size();++i) {
            auto segment=subtract(branch[i],branch[i-1]),offset=subtract(world,branch[i-1]);
            double lengthSquared=0,dot=0;for(int a=0;a<3;++a) {lengthSquared+=segment[a]*segment[a];dot+=offset[a]*segment[a];}
            double t=lengthSquared==0?0:std::clamp(dot/lengthSquared,0.0,1.0);
            for(int a=0;a<3;++a) offset[a]-=t*segment[a];
            distance=std::min(distance,norm(offset));
        }
        result.source.at(x,y,z)=static_cast<float>(300*std::exp(-0.5*(distance/radiusMm)*(distance/radiusMm))+
            (noiseStddev>0?noise(random):0));
        result.reference.at(x,y,z)=distance<=radiusMm?1.0f:0.0f;
    }
    return result;
}
}

#pragma once
#include "core/Volume3D.hpp"
#include <cstdint>
#include <queue>

namespace medvessel {
using VoxelIndex=std::array<std::size_t,3>;
struct RegionGrowingOptions {
    float lowThreshold=0.1f,highThreshold=0.3f;
    float minIntensity=-std::numeric_limits<float>::infinity();
    float maxIntensity=std::numeric_limits<float>::infinity();
    int connectivity=26;
};
inline void validateRegionOptions(const RegionGrowingOptions& o) {
    if(!std::isfinite(o.lowThreshold)||!std::isfinite(o.highThreshold)||
       o.lowThreshold<0||o.highThreshold>1||o.lowThreshold>o.highThreshold||
       std::isnan(o.minIntensity)||std::isnan(o.maxIntensity)||o.minIntensity>o.maxIntensity||
       (o.connectivity!=6&&o.connectivity!=26)) throw std::invalid_argument("Invalid region-growing options");
}
inline Volume3D<std::uint8_t> seededRegionGrowing3D(const Volume3D<float>& vesselness,
    const Volume3D<float>& source,const std::vector<VoxelIndex>& seeds,
    const RegionGrowingOptions& options={}) {
    validateRegionOptions(options);
    if(vesselness.dimensions()!=source.dimensions() ||
       vesselness.geometry().spacing!=source.geometry().spacing ||
       vesselness.geometry().origin!=source.geometry().origin ||
       vesselness.geometry().direction!=source.geometry().direction)
        throw std::invalid_argument("Source and vesselness grids differ");
    const auto d=source.dimensions();
    Volume3D<std::uint8_t> mask(d[0],d[1],d[2],source.geometry());
    auto eligible=[&](VoxelIndex p) {
        float v=vesselness.at(p[0],p[1],p[2]),s=source.at(p[0],p[1],p[2]);
        return std::isfinite(v)&&std::isfinite(s)&&v>=options.lowThreshold&&
            s>=options.minIntensity&&s<=options.maxIntensity;
    };
    std::queue<VoxelIndex> pending;
    for(auto p:seeds) {
        if(p[0]>=d[0]||p[1]>=d[1]||p[2]>=d[2]) throw std::out_of_range("Seed outside volume");
        if(eligible(p)&&!mask.at(p[0],p[1],p[2])) {
            mask.at(p[0],p[1],p[2])=1;pending.push(p);
        }
    }
    while(!pending.empty()) {
        auto p=pending.front();pending.pop();
        for(int dz=-1;dz<=1;++dz) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            int steps=std::abs(dx)+std::abs(dy)+std::abs(dz);
            if(steps==0||(options.connectivity==6&&steps!=1)) continue;
            const int delta[3]={dx,dy,dz};VoxelIndex q{};bool inside=true;
            for(int a=0;a<3;++a) {
                if((delta[a]<0&&p[a]==0)||(delta[a]>0&&p[a]==d[a]-1)) {inside=false;break;}
                q[a]=delta[a]<0?p[a]-1:delta[a]>0?p[a]+1:p[a];
            }
            if(inside&&!mask.at(q[0],q[1],q[2])&&eligible(q)) {
                mask.at(q[0],q[1],q[2])=1;pending.push(q);
            }
        }
    }
    return mask;
}
inline Volume3D<std::uint8_t> hysteresisThreshold3D(const Volume3D<float>& vesselness,
    const Volume3D<float>& source,const RegionGrowingOptions& options={}) {
    validateRegionOptions(options);
    std::vector<VoxelIndex> seeds;auto d=vesselness.dimensions();
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
        if(vesselness.at(x,y,z)>=options.highThreshold) seeds.push_back({x,y,z});
    return seededRegionGrowing3D(vesselness,source,seeds,options);
}
inline double dice(const Volume3D<std::uint8_t>& a,const Volume3D<std::uint8_t>& b) {
    if(a.dimensions()!=b.dimensions()||a.geometry().spacing!=b.geometry().spacing||
       a.geometry().origin!=b.geometry().origin||a.geometry().direction!=b.geometry().direction)
        throw std::invalid_argument("Dice grids differ");
    double countA=0,countB=0,intersection=0;
    for(std::size_t i=0;i<a.data().size();++i) {
        bool aa=a.data()[i]!=0,bb=b.data()[i]!=0;
        countA+=aa;countB+=bb;intersection+=aa&&bb;
    }
    return countA+countB==0?1:2*intersection/(countA+countB);
}
} // namespace medvessel

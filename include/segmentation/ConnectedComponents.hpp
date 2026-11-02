#pragma once
#include "segmentation/RegionGrowing.hpp"

namespace medvessel {
struct ComponentResult {
    Volume3D<std::uint32_t> labels;
    // Index zero represents background; counts[0] is always zero.
    std::vector<std::size_t> counts;
};
inline ComponentResult connectedComponents3D(const Volume3D<std::uint8_t>& mask,int connectivity=26) {
    if(connectivity!=6&&connectivity!=26) throw std::invalid_argument("Connectivity must be 6 or 26");
    auto d=mask.dimensions();
    ComponentResult result{Volume3D<std::uint32_t>(d[0],d[1],d[2],mask.geometry()),{0}};
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x) {
        if(!mask.at(x,y,z)||result.labels.at(x,y,z)) continue;
        if(result.counts.size()>std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("Too many components");
        auto label=static_cast<std::uint32_t>(result.counts.size());result.counts.push_back(0);
        std::queue<VoxelIndex> queue;queue.push({x,y,z});result.labels.at(x,y,z)=label;
        while(!queue.empty()) {
            auto p=queue.front();queue.pop();++result.counts[label];
            for(int dz=-1;dz<=1;++dz) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int steps=std::abs(dx)+std::abs(dy)+std::abs(dz);
                if(steps==0||(connectivity==6&&steps!=1)) continue;
                int delta[3]={dx,dy,dz};VoxelIndex q{};bool inside=true;
                for(int a=0;a<3;++a) {
                    if((delta[a]<0&&p[a]==0)||(delta[a]>0&&p[a]==d[a]-1)) {inside=false;break;}
                    q[a]=delta[a]<0?p[a]-1:delta[a]>0?p[a]+1:p[a];
                }
                if(inside&&mask.at(q[0],q[1],q[2])&&!result.labels.at(q[0],q[1],q[2])) {
                    result.labels.at(q[0],q[1],q[2])=label;queue.push(q);
                }
            }
        }
    }
    return result;
}
inline Volume3D<std::uint8_t> removeSmallComponents(const Volume3D<std::uint8_t>& mask,
    std::size_t minimumVoxels,int connectivity=26) {
    auto components=connectedComponents3D(mask,connectivity);auto d=mask.dimensions();auto result=mask;
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x) {
        auto label=components.labels.at(x,y,z);
        result.at(x,y,z)=label&&components.counts[label]>=minimumVoxels?1:0;
    }
    return result;
}
}

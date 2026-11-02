#pragma once
#include "skeleton/DistanceTransform.hpp"
#include "segmentation/RegionGrowing.hpp"

namespace medvessel {
namespace detail {
inline std::array<bool,27> localForeground(const Volume3D<std::uint8_t>& mask,VoxelIndex p) {
    std::array<bool,27> local{};auto d=mask.dimensions();
    for(int z=-1;z<=1;++z) for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        int delta[3]={x,y,z};VoxelIndex q{};bool inside=true;
        for(int a=0;a<3;++a) {
            if((delta[a]<0&&p[a]==0)||(delta[a]>0&&p[a]==d[a]-1)) {inside=false;break;}
            q[a]=delta[a]<0?p[a]-1:delta[a]>0?p[a]+1:p[a];
        }
        local[(z+1)*9+(y+1)*3+x+1]=inside&&mask.at(q[0],q[1],q[2])!=0;
    }
    return local;
}
inline int localComponents(const std::array<bool,27>& foreground,bool background) {
    std::array<bool,27> visited{};int components=0;
    auto allowed=[&](int i) {
        int x=i%3-1,y=i/3%3-1,z=i/9-1;
        if(i==13) return false;
        return background ? (!foreground[i]&&std::abs(x)+std::abs(y)+std::abs(z)<=2) : foreground[i];
    };
    for(int i=0;i<27;++i) if(!visited[i]&&allowed(i)) {
        std::array<int,27> queue{};int head=0,tail=0;queue[tail++]=i;visited[i]=true;
        bool touchesFace=false;
        while(head<tail) {
            int p=queue[head++],px=p%3,py=p/3%3,pz=p/9;
            if(std::abs(px-1)+std::abs(py-1)+std::abs(pz-1)==1) touchesFace=true;
            for(int q=0;q<27;++q) if(!visited[q]&&allowed(q)) {
                int dx=std::abs(q%3-px),dy=std::abs(q/3%3-py),dz=std::abs(q/9-pz);
                bool adjacent=background ? dx+dy+dz==1 : std::max({dx,dy,dz})==1;
                if(adjacent) {visited[q]=true;queue[tail++]=q;}
            }
        }
        if(!background||touchesFace) ++components;
    }
    return components;
}
}
inline bool isSimplePoint26(const Volume3D<std::uint8_t>& mask,VoxelIndex p) {
    if(!mask.at(p[0],p[1],p[2])) return false;
    auto local=detail::localForeground(mask,p);
    return detail::localComponents(local,false)==1&&detail::localComponents(local,true)==1;
}
// Sequential distance-priority homotopic erosion using (26 foreground,6 background).
// Requeue neighbours after deletion; protect endpoints and recheck simplicity.
// This baseline may leave sheets/spurs and is not Lee's thinning algorithm.
inline Volume3D<std::uint8_t> thinning3D(const Volume3D<std::uint8_t>& input) {
    auto mask=input;auto distance=distanceTransform3D(input);auto d=input.dimensions();
    using Candidate=std::pair<float,VoxelIndex>;
    std::priority_queue<Candidate,std::vector<Candidate>,std::greater<Candidate>> pending;
    Volume3D<std::uint8_t> queued(d[0],d[1],d[2],input.geometry());
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
        if(mask.at(x,y,z)) {pending.push({distance.at(x,y,z),{x,y,z}});queued.at(x,y,z)=1;}
    while(!pending.empty()) {
        auto p=pending.top().second;pending.pop();queued.at(p[0],p[1],p[2])=0;
        if(!mask.at(p[0],p[1],p[2])) continue;
        auto local=detail::localForeground(mask,p);
        int neighbors=0;for(int i=0;i<27;++i) if(i!=13&&local[i]) ++neighbors;
        if(neighbors<=1) continue;
        if(detail::localComponents(local,false)==1&&detail::localComponents(local,true)==1) {
            mask.at(p[0],p[1],p[2])=0;
            for(int dz=-1;dz<=1;++dz) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int delta[3]={dx,dy,dz};VoxelIndex q{};bool inside=true;
                for(int a=0;a<3;++a) {
                    if((delta[a]<0&&p[a]==0)||(delta[a]>0&&p[a]==d[a]-1)) {inside=false;break;}
                    q[a]=delta[a]<0?p[a]-1:delta[a]>0?p[a]+1:p[a];
                }
                if(inside&&mask.at(q[0],q[1],q[2])&&!queued.at(q[0],q[1],q[2])) {
                    pending.push({distance.at(q[0],q[1],q[2]),q});queued.at(q[0],q[1],q[2])=1;
                }
            }
        }
    }
    return mask;
}
} // namespace medvessel

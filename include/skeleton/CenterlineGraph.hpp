#pragma once
#include "segmentation/RegionGrowing.hpp"
#include "geometry/BranchAnalysis.hpp"
#include <set>
#include <map>

namespace medvessel {
struct VesselNode {
    Point3D positionWorld{};
    std::vector<std::size_t> adjacentBranches;
};
struct VesselBranch {
    std::size_t startNodeId=0,endNodeId=0;
    std::vector<Point3D> centerlineWorld;
    std::vector<VoxelIndex> voxels;
    double lengthMm=0;
};
struct CenterlineGraph {
    std::vector<VesselNode> nodes;
    std::vector<VesselBranch> branches;
};
// Compress degree-two chains under 26-connectivity. Connected junction voxels
// are clustered; degree-zero/one voxels remain individual nodes. A closed chain
// gets one synthetic node and a self-loop. Residual sheets must be inspected.
inline CenterlineGraph centerlineGraph(const Volume3D<std::uint8_t>& skeleton) {
    const auto d=skeleton.dimensions();
    std::vector<VoxelIndex> voxels;
    std::map<VoxelIndex,std::size_t> lookup;
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
        if(skeleton.at(x,y,z)) {lookup[{x,y,z}]=voxels.size();voxels.push_back({x,y,z});}
    const auto n=voxels.size();
    std::vector<std::vector<std::size_t>> neighbors(n);
    for(std::size_t i=0;i<n;++i) {
        auto p=voxels[i];
        for(int z=-1;z<=1;++z) for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
            if(x==0&&y==0&&z==0) continue;
            int delta[3]={x,y,z};VoxelIndex q{};bool inside=true;
            for(int a=0;a<3;++a) {
                if((delta[a]<0&&p[a]==0)||(delta[a]>0&&p[a]==d[a]-1)) {inside=false;break;}
                q[a]=delta[a]<0?p[a]-1:delta[a]>0?p[a]+1:p[a];
            }
            if(inside) {auto found=lookup.find(q);if(found!=lookup.end()) neighbors[i].push_back(found->second);}
        }
    }
    CenterlineGraph graph;
    constexpr auto missing=std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> nodeOf(n,missing);
    std::vector<std::vector<std::size_t>> members;
    auto createNode=[&](std::size_t first,bool junction) {
        const auto id=graph.nodes.size();
        std::vector<std::size_t> group{first};nodeOf[first]=id;
        for(std::size_t head=0;head<group.size();++head) if(junction)
            for(auto q:neighbors[group[head]]) if(neighbors[q].size()>2&&nodeOf[q]==missing) {
                nodeOf[q]=id;group.push_back(q);
            }
        Point3D position{};
        for(auto voxel:group) {
            auto p=voxels[voxel];auto world=skeleton.geometry().indexToWorld(
                {static_cast<double>(p[0]),static_cast<double>(p[1]),static_cast<double>(p[2])});
            for(int a=0;a<3;++a) position[a]+=world[a]/group.size();
        }
        graph.nodes.push_back({position,{}});members.push_back(std::move(group));
    };
    for(std::size_t i=0;i<n;++i) if(nodeOf[i]==missing&&neighbors[i].size()!=2)
        createNode(i,neighbors[i].size()>2);
    std::set<std::pair<std::size_t,std::size_t>> visited;
    auto key=[](std::size_t a,std::size_t b) {return std::make_pair(std::min(a,b),std::max(a,b));};
    auto trace=[&](std::size_t first,std::size_t second) {
        if(visited.count(key(first,second))) return;
        if(nodeOf[first]==nodeOf[second]) return;
        VesselBranch branch;branch.startNodeId=nodeOf[first];
        std::vector<std::size_t> path{first};auto previous=first,current=second;
        visited.insert(key(first,second));
        while(true) {
            path.push_back(current);
            if(nodeOf[current]!=missing) break;
            auto next=neighbors[current][0]==previous?neighbors[current][1]:neighbors[current][0];
            visited.insert(key(current,next));previous=current;current=next;
        }
        branch.endNodeId=nodeOf[current];
        for(auto id:path) {
            auto p=voxels[id];branch.voxels.push_back(p);
            branch.centerlineWorld.push_back(skeleton.geometry().indexToWorld(
                {static_cast<double>(p[0]),static_cast<double>(p[1]),static_cast<double>(p[2])}));
        }
        branch.lengthMm=polylineLengthMm(branch.centerlineWorld);
        auto id=graph.branches.size();
        graph.nodes[branch.startNodeId].adjacentBranches.push_back(id);
        graph.nodes[branch.endNodeId].adjacentBranches.push_back(id);
        graph.branches.push_back(std::move(branch));
    };
    for(std::size_t node=0;node<members.size();++node)
        for(auto p:members[node]) for(auto q:neighbors[p]) trace(p,q);
    // Components with degree two everywhere have no endpoint or junction.
    for(std::size_t p=0;p<n;++p) if(nodeOf[p]==missing) {
        bool unvisited=false;for(auto q:neighbors[p]) if(!visited.count(key(p,q))) unvisited=true;
        if(unvisited) {
            createNode(p,false);
            for(auto q:neighbors[p]) trace(p,q);
        }
    }
    return graph;
}
} // namespace medvessel

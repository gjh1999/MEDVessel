#pragma once
#include "skeleton/CenterlineGraph.hpp"

namespace medvessel {
// Explicit opt-in anatomical simplification. Never remove isolated full
// components or cycles; terminal paths shorter than the threshold are removed.
inline Volume3D<std::uint8_t> pruneShortBranches(const Volume3D<std::uint8_t>& skeleton,double minimumLengthMm) {
    if(!std::isfinite(minimumLengthMm)||minimumLengthMm<0) throw std::invalid_argument("Invalid pruning length");
    auto result=skeleton;if(minimumLengthMm==0) return result;
    bool changed=true;
    while(changed) {
        changed=false;auto graph=centerlineGraph(result);
        for(const auto& branch:graph.branches) {
            if(branch.lengthMm>=minimumLengthMm||branch.startNodeId==branch.endNodeId) continue;
            bool startLeaf=graph.nodes[branch.startNodeId].adjacentBranches.size()==1;
            bool endLeaf=graph.nodes[branch.endNodeId].adjacentBranches.size()==1;
            if(startLeaf==endLeaf||branch.voxels.size()<2) continue;
            std::size_t first=startLeaf?0:1,last=startLeaf?branch.voxels.size()-1:branch.voxels.size();
            for(std::size_t i=first;i<last;++i) {auto p=branch.voxels[i];result.at(p[0],p[1],p[2])=0;}
            changed=true;break; // Rebuild degrees before deciding on another branch.
        }
    }
    return result;
}
}

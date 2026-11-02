#pragma once
#include "skeleton/CenterlineGraph.hpp"
#include "geometry/CrossSection.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace medvessel {
inline void writeGeometryReports(const CenterlineGraph& graph,const Volume3D<std::uint8_t>& mask,
    const Volume3D<float>& distance,const std::filesystem::path& output) {
    std::ofstream sections(output/"sections.csv"),angles(output/"branch_angles.csv"),nodes(output/"nodes.csv");
    sections<<std::setprecision(12);angles<<std::setprecision(12);nodes<<std::setprecision(12);
    sections<<"branch_id,midpoint_area_mm2,midpoint_equivalent_diameter_mm,mean_curvature_per_mm,status\n";
    angles<<"node_id,branch_a,branch_b,outward_angle_degrees,fit_window_mm\n";
    nodes<<"node_id,x_ras_mm,y_ras_mm,z_ras_mm,degree\n";
    for(std::size_t i=0;i<graph.branches.size();++i) {
        const auto& branch=graph.branches[i];auto midpoint=branch.centerlineWorld.size()/2;
        auto curvature=polylineCurvature(branch.centerlineWorld,2);double sum=0;std::size_t valid=0;
        for(double v:curvature) if(std::isfinite(v)) {sum+=v;++valid;}
        sections<<i<<',';
        std::string status="ok";
        try {
            if(branch.centerlineWorld.size()<3) throw std::invalid_argument("Insufficient centreline samples");
            auto before=midpoint==0?0:midpoint-1,after=std::min(midpoint+1,branch.centerlineWorld.size()-1);
            auto tangent=subtract(branch.centerlineWorld[after],branch.centerlineWorld[before]);
            auto voxel=branch.voxels[midpoint];double radius=distance.at(voxel[0],voxel[1],voxel[2]);
            auto spacing=mask.geometry().spacing;double pixel=*std::min_element(spacing.begin(),spacing.end())/2;
            if(!std::isfinite(radius)) throw std::invalid_argument("Undefined radius");
            auto half=static_cast<std::size_t>(std::ceil((2*radius+2)/pixel));
            auto section=crossSection(mask,branch.centerlineWorld[midpoint],tangent,pixel,half);
            sections<<section.areaMm2<<','<<section.equivalentDiameterMm<<',';
        } catch(const std::invalid_argument&) {sections<<",,";status="undefined_or_truncated";}
        if(valid) sections<<sum/valid;
        sections<<','<<status<<'\n';
    }
    for(std::size_t i=0;i<graph.nodes.size();++i) {
        const auto& node=graph.nodes[i];auto p=node.positionWorld;
        nodes<<i<<','<<p[0]<<','<<p[1]<<','<<p[2]<<','<<node.adjacentBranches.size()<<'\n';
        for(std::size_t a=0;a<node.adjacentBranches.size();++a)
            for(std::size_t b=a+1;b<node.adjacentBranches.size();++b) {
                auto idA=node.adjacentBranches[a],idB=node.adjacentBranches[b];
                if(idA==idB) continue; // Self-loop appears twice in adjacency.
                auto outward=[&](std::size_t id) {
                    const auto& branch=graph.branches[id];auto path=branch.centerlineWorld;
                    if(branch.startNodeId!=i) std::reverse(path.begin(),path.end());
                    return path;
                };
                angles<<i<<','<<idA<<','<<idB<<',';
                try {angles<<branchAngleDegrees(outward(idA),outward(idB),3);}
                catch(const std::invalid_argument&) {}
                angles<<",3\n";
            }
    }
    sections.close();angles.close();nodes.close();
    if(!sections||!angles||!nodes) throw std::runtime_error("Cannot write geometry reports");
}
}

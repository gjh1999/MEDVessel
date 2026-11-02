#include "skeleton/CenterlineGraph.hpp"
#include "skeleton/Prune.hpp"
#include <iostream>
void require(bool v) {if(!v) throw std::runtime_error("Graph check failed");}
#include "Test.hpp"
MEDVESSEL_TEST(Graph) {
    using namespace medvessel;
    ImageGeometry g;g.spacing={0.5,1,2};
    Volume3D<std::uint8_t> line(9,9,9,g);
    for(std::size_t x=1;x<8;++x) line.at(x,4,4)=1;
    auto straight=centerlineGraph(line);
    require(pruneShortBranches(line,100).data()==line.data()); // Keep complete isolated branches.
    require(straight.nodes.size()==2&&straight.branches.size()==1);
    require(std::abs(straight.branches[0].lengthMm-3)<1e-8);
    Volume3D<std::uint8_t> yshape(11,11,11);
    for(std::size_t y=1;y<=5;++y) yshape.at(5,y,5)=1;
    for(std::size_t i=1;i<=4;++i) {yshape.at(5-i,5+i,5)=1;yshape.at(5+i,5+i,5)=1;}
    auto tree=centerlineGraph(yshape);
    require(tree.nodes.size()==4&&tree.branches.size()==3);
    int junctions=0;for(auto node:tree.nodes) junctions+=node.adjacentBranches.size()==3;
    require(junctions==1);
    require(centerlineGraph(pruneShortBranches(yshape,4.5)).branches.size()==1);
    // Diamond loop avoids diagonal shortcuts at square corners under 26-connectivity.
    Volume3D<std::uint8_t> ring(9,9,3);
    for(int i=0;i<3;++i) {
        ring.at(4+i,1+i,1)=1;ring.at(7-i,4+i,1)=1;
        ring.at(4-i,7-i,1)=1;ring.at(1+i,4-i,1)=1;
    }
    auto cycle=centerlineGraph(ring);
    require(cycle.nodes.size()==1&&cycle.branches.size()==1);
    require(cycle.branches[0].startNodeId==cycle.branches[0].endNodeId);
    require(pruneShortBranches(ring,100).data()==ring.data());
    Volume3D<std::uint8_t> isolated(3,3,3);isolated.at(1,1,1)=1;
    auto point=centerlineGraph(isolated);require(point.nodes.size()==1&&point.branches.empty());
    std::cout<<"Centerline graph tests passed\n";
}

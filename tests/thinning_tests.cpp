#include "skeleton/Thinning.hpp"
#include <iostream>
void require(bool v) {if(!v) throw std::runtime_error("Topology check failed");}
std::size_t count(const medvessel::Volume3D<std::uint8_t>& v) {
    std::size_t n=0;for(auto p:v.data()) n+=p!=0;return n;
}
#include "Test.hpp"
MEDVESSEL_TEST(Thinning) {
    using namespace medvessel;
    Volume3D<std::uint8_t> line(9,9,9);
    for(std::size_t x=1;x<8;++x) line.at(x,4,4)=1;
    require(!isSimplePoint26(line,{4,4,4}));
    require(isSimplePoint26(line,{1,4,4}));
    require(thinning3D(line).data()==line.data());
    Volume3D<std::uint8_t> tube(11,11,15);
    for(std::size_t z=2;z<13;++z) for(std::size_t y=3;y<8;++y) for(std::size_t x=3;x<8;++x)
        tube.at(x,y,z)=1;
    auto skeleton=thinning3D(tube);
    require(count(skeleton)>1&&count(skeleton)<count(tube));
    // Every remaining foreground voxel belongs to one 26-connected component.
    Volume3D<float> response(11,11,15),source(11,11,15);std::vector<VoxelIndex> seeds;
    for(std::size_t z=0;z<15;++z) for(std::size_t y=0;y<11;++y) for(std::size_t x=0;x<11;++x)
        if(skeleton.at(x,y,z)) {response.at(x,y,z)=1;if(seeds.empty()) seeds.push_back({x,y,z});}
    auto component=seededRegionGrowing3D(response,source,seeds);
    require(count(component)==count(skeleton));
    require(thinning3D(skeleton).data()==skeleton.data());
    Volume3D<std::uint8_t> shell(5,5,5,{},1);
    for(std::size_t z=1;z<4;++z) for(std::size_t y=1;y<4;++y) for(std::size_t x=1;x<4;++x)
        shell.at(x,y,z)=0;
    require(!isSimplePoint26(shell,{2,2,0})); // Cannot join cavity to exterior.
    Volume3D<std::uint8_t> ring(7,7,3);
    for(std::size_t i=1;i<6;++i) {ring.at(i,1,1)=1;ring.at(i,5,1)=1;ring.at(1,i,1)=1;ring.at(5,i,1)=1;}
    auto loop=thinning3D(ring);
    require(!loop.at(3,3,1)&&count(loop)>4);
    std::cout<<"Thinning tests passed; cylinder "<<count(tube)<<" -> "<<count(skeleton)<<" voxels\n";
}

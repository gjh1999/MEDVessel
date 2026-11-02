#include "segmentation/RegionGrowing.hpp"
#include "segmentation/ConnectedComponents.hpp"
#include <iostream>
void require(bool condition) {if(!condition) throw std::runtime_error("Segmentation check failed");}
#include "Test.hpp"
MEDVESSEL_TEST(Segmentation) {
    using namespace medvessel;
    Volume3D<float> source(7,7,7,{},300),response(7,7,7);
    response.at(1,1,1)=0.9f;response.at(2,1,1)=0.2f;response.at(3,1,1)=0.2f;
    response.at(5,5,5)=0.2f;response.at(4,2,1)=0.2f;
    RegionGrowingOptions o;o.connectivity=6;
    auto mask=hysteresisThreshold3D(response,source,o);
    require(mask.at(3,1,1)==1&&mask.at(5,5,5)==0&&mask.at(4,2,1)==0);
    o.connectivity=26;
    auto diagonal=hysteresisThreshold3D(response,source,o);
    require(diagonal.at(4,2,1)==1);
    source.at(2,1,1)=1000;o.maxIntensity=500;
    auto gated=seededRegionGrowing3D(response,source,{{1,1,1}},o);
    require(gated.at(2,1,1)==0&&gated.at(3,1,1)==0);
    require(dice(mask,mask)==1);
    Volume3D<std::uint8_t> empty(7,7,7);
    require(dice(empty,empty)==1&&dice(mask,empty)==0);
    bool rejected=false;
    try {seededRegionGrowing3D(response,source,{{7,0,0}},o);}
    catch(const std::out_of_range&) {rejected=true;}
    require(rejected);
    auto components=connectedComponents3D(diagonal,26);
    require(components.counts.size()==2&&components.counts[1]==4);
    Volume3D<std::uint8_t> pieces(5,5,5);
    pieces.at(0,0,0)=1;pieces.at(1,1,1)=1;pieces.at(3,3,3)=1;
    require(connectedComponents3D(pieces,6).counts.size()==4);
    require(connectedComponents3D(pieces,26).counts.size()==3);
    auto filtered=removeSmallComponents(pieces,2);
    require(filtered.at(0,0,0)==1&&filtered.at(1,1,1)==1&&filtered.at(3,3,3)==0);
    std::cout<<"Segmentation tests passed\n";
}

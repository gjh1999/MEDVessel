#include "segmentation/RegionGrowing.hpp"
#include "segmentation/ConnectedComponents.hpp"
#include "segmentation/SeededCoreGrowth.hpp"
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
    // A weak bridge may expand only from the original core, not from each
    // newly accepted voxel. Spacing, not voxel count, defines the band.
    ImageGeometry anisotropic;anisotropic.spacing={.5,1,2};
    anisotropic.direction={-1,0,0,0,0,-1,0,1,0};
    Volume3D<float> ct(15,5,3,anisotropic,300),vesselness(15,5,3,anisotropic);
    for(std::size_t x=1;x<=6;++x) vesselness.at(x,2,1)=.9f;
    for(std::size_t x=7;x<=13;++x) vesselness.at(x,2,1)=.2f;
    vesselness.at(2,3,1)=.2f;vesselness.at(2,2,2)=.2f;
    RegionGrowingOptions bounded;bounded.lowThreshold=.1f;bounded.highThreshold=.5f;bounded.connectivity=6;
    auto leaking=seededRegionGrowing3D(vesselness,ct,{{2,2,1}},bounded);
    require(leaking.at(13,2,1));
    auto limited=seededCoreGrowth3D(vesselness,ct,{{2,2,1}},bounded,.75,0);
    require(limited.mask.at(7,2,1)&&!limited.mask.at(8,2,1));
    require(!limited.mask.at(2,3,1)&&!limited.mask.at(2,2,2));
    auto wider=seededCoreGrowth3D(vesselness,ct,{{2,2,1}},bounded,1.1,0);
    require(wider.mask.at(2,3,1)&&!wider.mask.at(2,2,2));
    // A geometrically close but disconnected weak island must not be filled.
    vesselness.at(4,4,1)=.2f;
    auto disconnected=seededCoreGrowth3D(vesselness,ct,{{2,2,1},{4,4,1}},bounded,2.1,0);
    require(!disconnected.mask.at(4,4,1));
    // Duplicate seeds do not change the median; invalid/weak seeds cannot
    // create a new island during the second stage.
    vesselness.at(5,2,1)=.7f;
    auto normalized=seededCoreGrowth3D(vesselness,ct,{{2,2,1},{5,2,1},{2,2,1}},bounded,.75,.5);
    require(std::abs(normalized.coreThreshold-.4f)<1e-6);
    auto normalizedUnique=seededCoreGrowth3D(vesselness,ct,{{2,2,1},{5,2,1}},bounded,.75,.5);
    require(normalizedUnique.mask.data()==normalized.mask.data());
    auto noCore=seededCoreGrowth3D(vesselness,ct,{{13,2,1}},bounded,.75,0);
    require(std::count(noCore.mask.data().begin(),noCore.mask.data().end(),std::uint8_t{1})==0);
    auto noSeeds=seededCoreGrowth3D(vesselness,ct,{},bounded,.75,.35);
    require(std::count(noSeeds.mask.data().begin(),noSeeds.mask.data().end(),std::uint8_t{1})==0);
    for(double invalid:{-1.,0.,std::numeric_limits<double>::infinity()}) {
        bool failed=false;try {seededCoreGrowth3D(vesselness,ct,{{2,2,1}},bounded,invalid);}catch(const std::invalid_argument&){failed=true;}
        require(failed);
    }
    std::cout<<"Segmentation tests passed\n";
}

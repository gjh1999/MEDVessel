#pragma once
#include "segmentation/RegionGrowing.hpp"
#include "skeleton/DistanceTransform.hpp"
#include <algorithm>
#include <utility>

namespace medvessel {
struct SeededCoreGrowthResult {
    Volume3D<std::uint8_t> mask;
    Volume3D<std::uint8_t> core;
    float coreThreshold;
};

// Development-mode two-stage segmentation. The fixed core is never expanded
// when defining the distance band: weak bridges cannot repeatedly renew it.
// No reference labels, anatomy coordinates, or reference-derived radii are read.
inline SeededCoreGrowthResult seededCoreGrowth3D(const Volume3D<float>& vesselness,
    const Volume3D<float>& source,const std::vector<VoxelIndex>& seeds,
    const RegionGrowingOptions& options,double bandMm,double seedCoreRatio=.35) {
    validateRegionOptions(options);
    validateRegionGrids(vesselness,source);
    if(!std::isfinite(bandMm)||bandMm<=0||!std::isfinite(seedCoreRatio)||seedCoreRatio<0||seedCoreRatio>1)
        throw std::invalid_argument("Core growth requires a positive band and a seed ratio in [0,1]");
    const auto dimensions=source.dimensions();
    auto uniqueSeeds=seeds;
    std::sort(uniqueSeeds.begin(),uniqueSeeds.end());
    uniqueSeeds.erase(std::unique(uniqueSeeds.begin(),uniqueSeeds.end()),uniqueSeeds.end());
    std::vector<float> responses;
    std::vector<VoxelIndex> viableSeeds;
    for(const auto& p:uniqueSeeds) {
        if(p[0]>=dimensions[0]||p[1]>=dimensions[1]||p[2]>=dimensions[2])
            throw std::out_of_range("Seed outside volume");
        float value=vesselness.at(p[0],p[1],p[2]),intensity=source.at(p[0],p[1],p[2]);
        if(std::isfinite(value)&&std::isfinite(intensity)&&value>=options.lowThreshold&&
           intensity>=options.minIntensity&&intensity<=options.maxIntensity) {
            responses.push_back(value);
            viableSeeds.push_back(p);
        }
    }
    auto coreOptions=options;
    if(seedCoreRatio==0) coreOptions.lowThreshold=options.highThreshold;
    else if(!responses.empty()) {
        std::sort(responses.begin(),responses.end());
        const auto mid=responses.size()/2;
        double median=responses.size()%2 ? responses[mid] : (double(responses[mid-1])+responses[mid])/2;
        coreOptions.lowThreshold=std::max(options.lowThreshold,static_cast<float>(seedCoreRatio*median));
    }
    coreOptions.highThreshold=std::max(coreOptions.highThreshold,coreOptions.lowThreshold);
    auto core=seededRegionGrowing3D(vesselness,source,viableSeeds,coreOptions);
    std::vector<VoxelIndex> activeSeeds;
    for(const auto& p:viableSeeds) if(core.at(p[0],p[1],p[2])) activeSeeds.push_back(p);
    if(activeSeeds.empty()) {
        Volume3D<std::uint8_t> empty(dimensions[0],dimensions[1],dimensions[2],source.geometry());
        return {std::move(empty),std::move(core),coreOptions.lowThreshold};
    }
    Volume3D<std::uint8_t> domain(dimensions[0],dimensions[1],dimensions[2],source.geometry());
    for(std::size_t z=0;z<dimensions[2];++z) for(std::size_t y=0;y<dimensions[1];++y)
        for(std::size_t x=0;x<dimensions[0];++x) domain.at(x,y,z)=!core.at(x,y,z);
    auto distance=distanceTransform3D(domain);
    const double tolerance=4*std::numeric_limits<float>::epsilon()*std::max(1.,bandMm);
    for(std::size_t z=0;z<dimensions[2];++z) for(std::size_t y=0;y<dimensions[1];++y)
        for(std::size_t x=0;x<dimensions[0];++x) domain.at(x,y,z)=distance.at(x,y,z)<=bandMm+tolerance;
    auto mask=seededRegionGrowing3D(vesselness,source,activeSeeds,options,&domain);
    return {std::move(mask),std::move(core),coreOptions.lowThreshold};
}
} // namespace medvessel

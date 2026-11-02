#pragma once
#include "skeleton/DistanceTransform.hpp"
#include "segmentation/RegionGrowing.hpp"

namespace medvessel {
// Six-neighbour foreground boundary, treating the exterior as background.
inline Volume3D<std::uint8_t> boundaryMask(const Volume3D<std::uint8_t>& mask) {
    auto d=mask.dimensions();Volume3D<std::uint8_t> result(d[0],d[1],d[2],mask.geometry());
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x) {
        if(!mask.at(x,y,z)) continue;
        bool boundary=x==0||y==0||z==0||x==d[0]-1||y==d[1]-1||z==d[2]-1;
        if(!boundary) boundary=!mask.at(x-1,y,z)||!mask.at(x+1,y,z)||!mask.at(x,y-1,z)||
            !mask.at(x,y+1,z)||!mask.at(x,y,z-1)||!mask.at(x,y,z+1);
        result.at(x,y,z)=boundary;
    }
    return result;
}
inline std::vector<double> directedVoxelDistances(const Volume3D<std::uint8_t>& from,
                                                 const Volume3D<std::uint8_t>& to) {
    // Reuse Dice grid validation without allocating another geometry wrapper.
    (void)dice(from,to);auto d=from.dimensions();
    Volume3D<std::uint8_t> complement(d[0],d[1],d[2],to.geometry());
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
        complement.at(x,y,z)=to.at(x,y,z)?0:1;
    auto distances=distanceTransform3D(complement);std::vector<double> values;
    for(std::size_t i=0;i<from.data().size();++i) if(from.data()[i]) values.push_back(distances.data()[i]);
    return values;
}
inline double hausdorff95Mm(const Volume3D<std::uint8_t>& a,const Volume3D<std::uint8_t>& b) {
    auto ba=boundaryMask(a),bb=boundaryMask(b);
    auto ab=directedVoxelDistances(ba,bb),reverse=directedVoxelDistances(bb,ba);
    if(ab.empty()&&reverse.empty()) return 0;
    if(ab.empty()||reverse.empty()) return std::numeric_limits<double>::infinity();
    // Define symmetric HD95 as max of directed nearest-rank 95th percentiles.
    auto percentile=[](std::vector<double>& values) {
        std::sort(values.begin(),values.end());
        auto rank=static_cast<std::size_t>(std::ceil(0.95*values.size()))-1;
        return values[rank];
    };
    return std::max(percentile(ab),percentile(reverse));
}
inline double clDice(const Volume3D<std::uint8_t>& prediction,const Volume3D<std::uint8_t>& reference,
    const Volume3D<std::uint8_t>& predictionSkeleton,const Volume3D<std::uint8_t>& referenceSkeleton) {
    (void)dice(prediction,reference);(void)dice(prediction,predictionSkeleton);(void)dice(reference,referenceSkeleton);
    double predicted=0,coveredPredicted=0,actual=0,coveredActual=0;
    for(std::size_t i=0;i<prediction.data().size();++i) {
        bool ps=predictionSkeleton.data()[i]!=0,rs=referenceSkeleton.data()[i]!=0;
        predicted+=ps;actual+=rs;
        coveredPredicted+=ps&&reference.data()[i]!=0;
        coveredActual+=rs&&prediction.data()[i]!=0;
    }
    if(predicted==0&&actual==0) return 1;
    if(predicted==0||actual==0) return 0;
    double precision=coveredPredicted/predicted,sensitivity=coveredActual/actual;
    return precision+sensitivity==0?0:2*precision*sensitivity/(precision+sensitivity);
}
} // namespace medvessel

#pragma once
#include "core/Volume3D.hpp"
#include <algorithm>
#include <optional>

namespace medvessel {
struct DisplayGrid {
    ImageGeometry geometry;
    std::array<std::size_t,3> dimensions{};
};

// RAS-aligned voxel-centre bounding box. Sampling is performed per visible
// plane: this descriptor never allocates another three-dimensional volume.
inline DisplayGrid anatomicalGrid(const Volume3D<float>& source) {
    DisplayGrid grid;
    double step=*std::min_element(source.geometry().spacing.begin(),source.geometry().spacing.end());
    grid.geometry.spacing={step,step,step};
    Point3D low{},high{};
    low.fill(std::numeric_limits<double>::infinity());high.fill(-std::numeric_limits<double>::infinity());
    for(int corner=0;corner<8;++corner){
        Point3D index{};
        for(int a=0;a<3;++a)index[a]=(corner&(1<<a))?double(source.dimensions()[a]-1):0.;
        auto world=source.geometry().indexToWorld(index);
        for(int a=0;a<3;++a){low[a]=std::min(low[a],world[a]);high[a]=std::max(high[a],world[a]);}
    }
    grid.geometry.origin=low;
    for(int a=0;a<3;++a){
        double intervals=(high[a]-low[a])/step;
        // Snap roundoff near integral ratios without dropping a real endpoint.
        if(std::abs(intervals-std::round(intervals))<1e-8)intervals=std::round(intervals);
        double count=std::ceil(intervals)+1;
        if(!std::isfinite(count)||count<1||count>std::numeric_limits<int>::max())
            throw std::overflow_error("Anatomical display dimension exceeds slider range");
        grid.dimensions[a]=std::size_t(count);
    }
    return grid;
}

inline bool clampSampleIndex(Point3D& index,const std::array<std::size_t,3>& dimensions) {
    for(int a=0;a<3;++a){
        double maximum=double(dimensions[a]-1);
        if(!std::isfinite(index[a])||index[a]<-1e-7||index[a]>maximum+1e-7)return false;
        index[a]=std::clamp(index[a],0.,maximum);
    }
    return true;
}

inline std::optional<std::array<std::size_t,3>> nearestSampleIndex(
    Point3D index,const std::array<std::size_t,3>& dimensions) {
    if(!clampSampleIndex(index,dimensions))return {};
    std::array<std::size_t,3> q{};
    for(int a=0;a<3;++a)q[a]=std::size_t(std::floor(index[a]+.5));
    return q;
}

inline float linearSample(const Volume3D<float>& source,Point3D index) {
    if(!clampSampleIndex(index,source.dimensions()))return std::numeric_limits<float>::quiet_NaN();
    std::array<std::size_t,3> low{},high{};Point3D fraction{};
    for(int a=0;a<3;++a){low[a]=std::size_t(std::floor(index[a]));high[a]=std::min(low[a]+1,source.dimensions()[a]-1);fraction[a]=index[a]-low[a];}
    double value=0;
    for(int corner=0;corner<8;++corner){std::array<std::size_t,3> q{};double weight=1;
        for(int a=0;a<3;++a){bool upper=corner&(1<<a);q[a]=upper?high[a]:low[a];weight*=upper?fraction[a]:1-fraction[a];}
        // Zero-weight nonfinite neighbours must not contaminate an exact sample.
        if(weight>0)value+=weight*source.at(q[0],q[1],q[2]);
    }
    return float(value);
}
}

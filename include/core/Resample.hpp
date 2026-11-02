#pragma once
#include "core/Volume3D.hpp"
#include <algorithm>

namespace medvessel {
// Label-preserving resampling on the same voxel-centre domain as linear.
template<class T>
Volume3D<T> resampleNearest(const Volume3D<T>& source,
    std::array<std::size_t,3> dimensions,const ImageGeometry& geometry,T outside=T{}) {
    Volume3D<T> output(dimensions[0],dimensions[1],dimensions[2],geometry,outside);
    const auto d=source.dimensions();
    for(std::size_t z=0;z<dimensions[2];++z) for(std::size_t y=0;y<dimensions[1];++y)
        for(std::size_t x=0;x<dimensions[0];++x) {
            auto p=source.geometry().worldToIndex(geometry.indexToWorld(
                {static_cast<double>(x),static_cast<double>(y),static_cast<double>(z)}));
            std::array<std::size_t,3> q{};bool inside=true;
            for(int a=0;a<3;++a) {
                double maximum=static_cast<double>(d[a]-1);
                if(!std::isfinite(p[a])||p[a]<-1e-8||p[a]>maximum+1e-8) {inside=false;break;}
                q[a]=static_cast<std::size_t>(std::floor(std::clamp(p[a],0.0,maximum)+0.5));
            }
            if(inside) output.at(x,y,z)=source.at(q[0],q[1],q[2]);
        }
    return output;
}
// Output geometry and dimensions are explicit. Outside the source centre domain,
// use a constant fill value. Downsampling callers must apply an anti-alias filter.
inline Volume3D<float> resampleLinear(const Volume3D<float>& source,
    std::array<std::size_t, 3> dimensions, const ImageGeometry& geometry,
    float outside = 0) {
    Volume3D<float> output(dimensions[0], dimensions[1], dimensions[2], geometry, outside);
    const auto dims = source.dimensions();
    for (std::size_t z=0; z<dimensions[2]; ++z)
        for (std::size_t y=0; y<dimensions[1]; ++y)
            for (std::size_t x=0; x<dimensions[0]; ++x) {
                auto p = source.geometry().worldToIndex(geometry.indexToWorld(
                    {static_cast<double>(x),static_cast<double>(y),static_cast<double>(z)}));
                bool inside = true;
                for (std::size_t a=0; a<3; ++a) {
                    const double maximum = static_cast<double>(dims[a]-1);
                    if (!std::isfinite(p[a]) || p[a]<-1e-8 || p[a]>maximum+1e-8) inside=false;
                    p[a]=std::clamp(p[a],0.0,maximum);
                }
                if (!inside) continue;
                std::array<std::size_t,3> lo{},hi{};
                Point3D fraction{};
                for (std::size_t a=0; a<3; ++a) {
                    lo[a]=static_cast<std::size_t>(std::floor(p[a]));
                    hi[a]=std::min(lo[a]+1,dims[a]-1);
                    fraction[a]=p[a]-static_cast<double>(lo[a]);
                }
                double value=0;
                for (int corner=0; corner<8; ++corner) {
                    double weight=1;
                    std::array<std::size_t,3> q{};
                    for (std::size_t a=0; a<3; ++a) {
                        bool upper=(corner & (1<<a))!=0;
                        q[a]=upper ? hi[a] : lo[a];
                        weight*=upper ? fraction[a] : 1-fraction[a];
                    }
                    value+=weight*source.at(q[0],q[1],q[2]);
                }
                output.at(x,y,z)=static_cast<float>(value);
            }
    return output;
}
} // namespace medvessel

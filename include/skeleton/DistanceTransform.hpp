#pragma once
#include "core/Volume3D.hpp"
#include <cstdint>
#include <algorithm>

namespace medvessel {
namespace detail {
// Lower envelope of parabolas f[q] + spacing^2 * (p-q)^2.
// Infinite candidates are skipped so entirely foreground scanlines remain valid.
inline std::vector<double> squaredDistanceLine(const std::vector<double>& f,double spacing) {
    const std::size_t n=f.size();
    if(n>static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())/sizeof(double)-1)
        throw std::length_error("Distance scanline too large");
    std::vector<double> result(n,std::numeric_limits<double>::infinity()),breaks(n+1);
    std::vector<std::size_t> sites;
    sites.reserve(n);
    std::size_t count=0;
    const double weight=spacing*spacing;
    if(!std::isfinite(weight)||weight==0) throw std::invalid_argument("Spacing squared is outside numeric range");
    for(std::size_t q=0;q<n;++q) {
        if(!std::isfinite(f[q])) continue;
        double intersection=-std::numeric_limits<double>::infinity();
        while(count>0) {
            auto p=sites[count-1];
            double qq=static_cast<double>(q),pp=static_cast<double>(p);
            intersection=((f[q]-f[p])/weight+(qq-pp)*(qq+pp))/(2*(qq-pp));
            if(intersection>breaks[count-1]) break;
            --count;
        }
        if(count==sites.size()) sites.push_back(q);
        else sites[count]=q;
        breaks[count]=count==0?-std::numeric_limits<double>::infinity():intersection;
        ++count;breaks[count]=std::numeric_limits<double>::infinity();
    }
    if(count==0) return result;
    std::size_t envelope=0;
    for(std::size_t q=0;q<n;++q) {
        while(envelope+1<count&&breaks[envelope+1]<static_cast<double>(q)) ++envelope;
        double delta=static_cast<double>(q)-static_cast<double>(sites[envelope]);
        result[q]=f[sites[envelope]]+weight*delta*delta;
    }
    return result;
}
}
// Distance to background voxel centres within the volume. No implicit exterior
// background: an all-foreground mask returns infinity; pad explicitly if needed.
inline Volume3D<float> distanceTransform3D(const Volume3D<std::uint8_t>& mask) {
    auto d=mask.dimensions();
    Volume3D<double> squared(d[0],d[1],d[2],mask.geometry());
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
        squared.at(x,y,z)=mask.at(x,y,z)?std::numeric_limits<double>::infinity():0;
    for(std::size_t axis=0;axis<3;++axis) {
        const auto a=(axis+1)%3,b=(axis+2)%3;
        for(std::size_t i=0;i<d[a];++i) for(std::size_t j=0;j<d[b];++j) {
            std::array<std::size_t,3> p{};p[a]=i;p[b]=j;
            std::vector<double> line(d[axis]);
            for(std::size_t k=0;k<d[axis];++k) {p[axis]=k;line[k]=squared.at(p[0],p[1],p[2]);}
            auto transformed=detail::squaredDistanceLine(line,mask.geometry().spacing[axis]);
            for(std::size_t k=0;k<d[axis];++k) {p[axis]=k;squared.at(p[0],p[1],p[2])=transformed[k];}
        }
    }
    Volume3D<float> result(d[0],d[1],d[2],mask.geometry());
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
        result.at(x,y,z)=static_cast<float>(std::sqrt(squared.at(x,y,z)));
    return result;
}
} // namespace medvessel

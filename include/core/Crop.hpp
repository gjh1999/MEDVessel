#pragma once
#include "core/Volume3D.hpp"

namespace medvessel {
template<class T>
Volume3D<T> cropVolume(const Volume3D<T>& source,std::array<std::size_t,3> start,
                     std::array<std::size_t,3> size) {
    auto dimensions=source.dimensions();
    for(int a=0;a<3;++a) if(!size[a]||start[a]>=dimensions[a]||size[a]>dimensions[a]-start[a])
        throw std::out_of_range("ROI outside volume");
    auto geometry=source.geometry();
    geometry.origin=geometry.indexToWorld({static_cast<double>(start[0]),static_cast<double>(start[1]),static_cast<double>(start[2])});
    Volume3D<T> output(size[0],size[1],size[2],geometry);
    for(std::size_t z=0;z<size[2];++z) for(std::size_t y=0;y<size[1];++y) for(std::size_t x=0;x<size[0];++x)
        output.at(x,y,z)=source.at(x+start[0],y+start[1],z+start[2]);
    return output;
}
}

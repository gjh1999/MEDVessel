#pragma once
#include "filters/Frangi.hpp"

namespace medvessel {
namespace detail {
inline double sampleIndexLinear(const Volume3D<float>& image,Point3D p) {
    auto d=image.dimensions();std::array<std::size_t,3> lo{},hi{};Point3D fraction{};
    for(int a=0;a<3;++a) {
        if(!std::isfinite(p[a])||p[a]<0||p[a]>static_cast<double>(d[a]-1)) return 0;
        lo[a]=static_cast<std::size_t>(std::floor(p[a]));hi[a]=std::min(lo[a]+1,d[a]-1);
        fraction[a]=p[a]-lo[a];
    }
    double result=0;
    for(int corner=0;corner<8;++corner) {
        double weight=1;std::array<std::size_t,3> q{};
        for(int a=0;a<3;++a) {bool upper=(corner&(1<<a))!=0;q[a]=upper?hi[a]:lo[a];weight*=upper?fraction[a]:1-fraction[a];}
        result+=weight*image.at(q[0],q[1],q[2]);
    }
    return result;
}
}
// Four sampled directions in the plane perpendicular to the local vessel
// tangent. Useful as a seed map; applying NMS directly to a lumen mask shrinks it.
inline Volume3D<float> nonMaximumSuppression3D(const Volume3D<float>& source,
    const VesselnessResult& enhanced,const FrangiOptions& options,double stepMm) {
    if(!std::isfinite(stepMm)||stepMm<=0) throw std::invalid_argument("Invalid NMS step");
    for(const auto* image:{&enhanced.vesselness,&enhanced.bestScaleMm})
        if(image->dimensions()!=source.dimensions()||image->geometry().spacing!=source.geometry().spacing||
           image->geometry().origin!=source.geometry().origin||image->geometry().direction!=source.geometry().direction)
            throw std::invalid_argument("NMS grids differ");
    auto d=source.dimensions();Volume3D<float> result(d[0],d[1],d[2],source.geometry());
    for(double sigma:options.scalesMm) {
        auto h=hessian3D(source,sigma);
        for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x) {
            double centre=enhanced.vesselness.at(x,y,z);
            if(centre<=0||enhanced.bestScaleMm.at(x,y,z)!=static_cast<float>(sigma)) continue;
            double xx=h[0].at(x,y,z),xy=h[1].at(x,y,z),xz=h[2].at(x,y,z);
            double yy=h[3].at(x,y,z),yz=h[4].at(x,y,z),zz=h[5].at(x,y,z);
            auto eigen=eigenSymmetric3x3({xx,xy,xz,xy,yy,yz,xz,yz,zz});
            auto u=eigen.vectors[1],v=eigen.vectors[2];
            std::array<Point3D,4> directions{u,v,Point3D{},Point3D{}};
            for(int a=0;a<3;++a) {directions[2][a]=(u[a]+v[a])/std::sqrt(2.0);directions[3][a]=(u[a]-v[a])/std::sqrt(2.0);}
            bool maximum=true;
            for(auto direction:directions) for(int sign:{-1,1}) {
                Point3D p{static_cast<double>(x),static_cast<double>(y),static_cast<double>(z)};
                for(int a=0;a<3;++a) p[a]+=sign*stepMm*direction[a]/source.geometry().spacing[a];
                if(detail::sampleIndexLinear(enhanced.vesselness,p)>centre+1e-7) maximum=false;
            }
            if(maximum) result.at(x,y,z)=static_cast<float>(centre);
        }
    }
    return result;
}
}

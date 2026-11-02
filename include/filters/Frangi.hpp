#pragma once
#include "filters/Hessian.hpp"
#include "filters/EigenSolver.hpp"

namespace medvessel {
struct FrangiOptions {
    std::vector<double> scalesMm{0.5,1,2};
    double alpha=0.5,beta=0.5,c=100;
    bool brightVessels=true;
};
struct VesselnessResult {
    Volume3D<float> vesselness;
    Volume3D<float> bestScaleMm;
};
inline double frangiResponse(const Point3D& eigenvalues,const FrangiOptions& options) {
    double a=std::abs(eigenvalues[0]),b=std::abs(eigenvalues[1]),c=std::abs(eigenvalues[2]);
    if(b==0||c==0) return 0;
    if(options.brightVessels ? (eigenvalues[1]>=0||eigenvalues[2]>=0)
                            : (eigenvalues[1]<=0||eigenvalues[2]<=0)) return 0;
    double ra=b/c,rb=a/std::sqrt(b*c);
    double strength=std::hypot(a,std::hypot(b,c))/options.c;
    return (-std::expm1(-0.5*(ra/options.alpha)*(ra/options.alpha))) *
        std::exp(-0.5*(rb/options.beta)*(rb/options.beta)) *
        (-std::expm1(-0.5*strength*strength));
}
inline VesselnessResult frangi3D(const Volume3D<float>& input,const FrangiOptions& options={}) {
    if(options.scalesMm.empty()) throw std::invalid_argument("Empty scale list");
    for(double p:{options.alpha,options.beta,options.c})
        if(!std::isfinite(p)||p<=0) throw std::invalid_argument("Invalid Frangi parameter");
    for(float v:input.data()) if(!std::isfinite(v)) throw std::invalid_argument("Nonfinite input");
    auto d=input.dimensions();
    VesselnessResult result{Volume3D<float>(d[0],d[1],d[2],input.geometry()),
                            Volume3D<float>(d[0],d[1],d[2],input.geometry())};
    for(double sigma:options.scalesMm) {
        auto h=hessian3D(input,sigma);
        for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y)
            for(std::size_t x=0;x<d[0];++x) {
                double xx=h[0].at(x,y,z),xy=h[1].at(x,y,z),xz=h[2].at(x,y,z);
                double yy=h[3].at(x,y,z),yz=h[4].at(x,y,z),zz=h[5].at(x,y,z);
                auto eigen=eigenSymmetric3x3({xx,xy,xz,xy,yy,yz,xz,yz,zz});
                float response=static_cast<float>(frangiResponse(eigen.values,options));
                if(response>result.vesselness.at(x,y,z)) {
                    result.vesselness.at(x,y,z)=response;
                    result.bestScaleMm.at(x,y,z)=static_cast<float>(sigma);
                }
            }
    }
    return result;
}
} // namespace medvessel

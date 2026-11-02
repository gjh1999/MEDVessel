#pragma once
#include "filters/Gaussian.hpp"

namespace medvessel {
namespace detail {
inline std::vector<double> derivativeKernel(double sigmaMm,double spacing,int order) {
    double sigma=sigmaMm/spacing;
    double r=std::ceil(3*sigma);
    if(!std::isfinite(r)||r>100000) throw std::invalid_argument("Derivative kernel too large");
    auto radius=static_cast<int>(r);
    std::vector<double> kernel(2*radius+1);
    double sum=0;
    for(int k=-radius;k<=radius;++k) {
        double u=k/sigma;
        double v=k==0 ? 1 : std::exp(-0.5*u*u);
        kernel[k+radius]=v;sum+=v;
    }
    for(auto& v:kernel) v/=sum;
    if(order==0) return kernel;
    // Correlation kernels: first derivative has positive first moment.
    double moment=0;
    for(int k=-radius;k<=radius;++k) {
        double x=k*spacing;
        kernel[k+radius]*=order==1 ? x : (x*x-sigmaMm*sigmaMm);
    }
    if(order==2) {
        double mean=0;
        for(double v:kernel) mean+=v;
        mean/=kernel.size();
        for(auto& v:kernel) v-=mean;
    }
    for(int k=-radius;k<=radius;++k) {
        double x=k*spacing;
        moment+=kernel[k+radius]*(order==1 ? x : x*x/2);
    }
    if(!std::isfinite(moment)||std::abs(moment)<1e-200)
        throw std::invalid_argument("Sigma too small for derivative sampling");
    for(auto& v:kernel) v/=moment;
    return kernel;
}
inline Volume3D<float> correlate(const Volume3D<float>& input,
                                const std::vector<double>& kernel,std::size_t axis) {
    auto dims=input.dimensions();
    Volume3D<float> output(dims[0],dims[1],dims[2],input.geometry());
    auto radius=static_cast<long long>(kernel.size()/2);
    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for(std::ptrdiff_t zz=0;zz<static_cast<std::ptrdiff_t>(dims[2]);++zz) {
      const auto z=static_cast<std::size_t>(zz);
      for(std::size_t y=0;y<dims[1];++y)
        for(std::size_t x=0;x<dims[0];++x) {
            std::array<std::size_t,3> p{x,y,z};double value=0;
            for(long long k=-radius;k<=radius;++k) {
                auto q=p;
                if(k<0) q[axis]=p[axis]<static_cast<std::size_t>(-k) ? 0 : p[axis]-static_cast<std::size_t>(-k);
                else q[axis]+=std::min(static_cast<std::size_t>(k),dims[axis]-1-p[axis]);
                value+=kernel[static_cast<std::size_t>(k+radius)]*input.at(q[0],q[1],q[2]);
            }
            output.at(x,y,z)=static_cast<float>(value);
        }
    }
    return output;
}
} // namespace detail
// Components xx, xy, xz, yy, yz, zz in the image-axis orthonormal basis.
// Eigenvalues are invariant to the image-to-world orthogonal transformation.
using HessianVolume=std::array<Volume3D<float>,6>;
inline HessianVolume hessian3D(const Volume3D<float>& input,double sigmaMm,
                               bool normalizeScale=true) {
    if(!std::isfinite(sigmaMm)||sigmaMm<=0) throw std::invalid_argument("Invalid sigma");
    const auto d=input.dimensions();
    auto blank=[&] {return Volume3D<float>(d[0],d[1],d[2],input.geometry());};
    HessianVolume result{blank(),blank(),blank(),blank(),blank(),blank()};
    const int orders[6][3]={{2,0,0},{1,1,0},{1,0,1},{0,2,0},{0,1,1},{0,0,2}};
    for(int component=0;component<6;++component) {
        auto filtered=input;
        for(std::size_t axis=0;axis<3;++axis)
            filtered=detail::correlate(filtered,detail::derivativeKernel(
                sigmaMm,input.geometry().spacing[axis],orders[component][axis]),axis);
        for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y)
            for(std::size_t x=0;x<d[0];++x)
                result[component].at(x,y,z)=filtered.at(x,y,z)*static_cast<float>(normalizeScale?sigmaMm*sigmaMm:1);
    }
    return result;
}
} // namespace medvessel

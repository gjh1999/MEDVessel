#include "filters/Gaussian.hpp"
#include "core/Resample.hpp"
#include <iostream>

// Deterministic synthetic smoke demo; no patient data required.
int main() {
    try {
        medvessel::ImageGeometry geometry;
        geometry.spacing={0.5,0.5,1.0};
        medvessel::Volume3D<float> phantom(33,33,17,geometry);
        for(std::size_t z=0;z<17;++z)
            for(std::size_t y=0;y<33;++y)
                for(std::size_t x=0;x<33;++x) {
                    double dx=(static_cast<double>(x)-16)*0.5;
                    double dy=(static_cast<double>(y)-16)*0.5;
                    phantom.at(x,y,z)=dx*dx+dy*dy<=4 ? 300.0f : 0.0f;
                }
        auto smoothed=medvessel::gaussianFilter3D(phantom,1.0);
        auto isotropic=geometry;
        isotropic.spacing={0.5,0.5,0.5};
        auto resampled=medvessel::resampleLinear(smoothed,{33,33,33},isotropic);
        std::cout<<"Synthetic cylinder: radius=2 mm, sigma=1 mm\n"
                 <<"Input: 33 x 33 x 17, spacing=0.5,0.5,1 mm\n"
                 <<"Output: 33 x 33 x 33, spacing=0.5,0.5,0.5 mm\n"
                 <<"Centre intensity: "<<resampled.at(16,16,16)<<'\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}

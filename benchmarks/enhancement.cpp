#include "filters/Frangi.hpp"
#include <chrono>
#include <iostream>
#ifdef _OPENMP
#include <omp.h>
#endif
int main() {
    medvessel::ImageGeometry geometry;geometry.spacing={0.5,0.5,1};
    medvessel::Volume3D<float> phantom(80,80,48,geometry);
    for(std::size_t z=0;z<48;++z) for(std::size_t y=0;y<80;++y) for(std::size_t x=0;x<80;++x) {
        double dx=(static_cast<double>(x)-40)*0.5,dy=(static_cast<double>(y)-40)*0.5;
        phantom.at(x,y,z)=static_cast<float>(300*std::exp(-(dx*dx+dy*dy)/8));
    }
    medvessel::FrangiOptions options;options.scalesMm={1,2};
    #ifdef _OPENMP
    omp_set_dynamic(0);omp_set_num_threads(1);
    #endif
    medvessel::frangi3D(phantom,options); // warm-up
    auto start=std::chrono::steady_clock::now();auto baseline=medvessel::frangi3D(phantom,options);
    double single=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"threads,seconds,max_response_difference,max_scale_difference\n1,"<<single<<",0,0\n";
    #ifdef _OPENMP
    omp_set_num_threads(4);
    start=std::chrono::steady_clock::now();auto parallel=medvessel::frangi3D(phantom,options);
    double multi=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    double difference=0,scaleDifference=0;
    for(std::size_t i=0;i<baseline.vesselness.data().size();++i) {
        difference=std::max(difference,static_cast<double>(std::abs(baseline.vesselness.data()[i]-parallel.vesselness.data()[i])));
        scaleDifference=std::max(scaleDifference,static_cast<double>(std::abs(baseline.bestScaleMm.data()[i]-parallel.bestScaleMm.data()[i])));
    }
    std::cout<<"4,"<<multi<<','<<difference<<','<<scaleDifference<<'\n';
    if(difference!=0||scaleDifference!=0) return 1;
    #endif
}

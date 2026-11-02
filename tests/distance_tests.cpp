#include "skeleton/DistanceTransform.hpp"
#include <iostream>
#include <random>
#include "Test.hpp"
MEDVESSEL_TEST(DistanceTransform) {
    using namespace medvessel;
    ImageGeometry g;g.spacing={0.4,1.3,2.5};
    std::mt19937 random(17);
    for(int trial=0;trial<15;++trial) {
        Volume3D<std::uint8_t> mask(7,6,5,g);
        for(std::size_t z=0;z<5;++z) for(std::size_t y=0;y<6;++y) for(std::size_t x=0;x<7;++x)
            mask.at(x,y,z)=random()%5!=0;
        auto distance=distanceTransform3D(mask);
        for(std::size_t z=0;z<5;++z) for(std::size_t y=0;y<6;++y) for(std::size_t x=0;x<7;++x) {
            double expected=std::numeric_limits<double>::infinity();
            for(std::size_t zz=0;zz<5;++zz) for(std::size_t yy=0;yy<6;++yy) for(std::size_t xx=0;xx<7;++xx)
                if(!mask.at(xx,yy,zz)) {
                    double dx=(static_cast<double>(x)-xx)*g.spacing[0];
                    double dy=(static_cast<double>(y)-yy)*g.spacing[1];
                    double dz=(static_cast<double>(z)-zz)*g.spacing[2];
                    expected=std::min(expected,std::sqrt(dx*dx+dy*dy+dz*dz));
                }
            if(std::abs(distance.at(x,y,z)-expected)>1e-5) throw std::runtime_error("EDT differs from exhaustive reference");
        }
    }
    Volume3D<std::uint8_t> full(3,3,3,{},1),empty(3,3,3);
    auto df=distanceTransform3D(full),de=distanceTransform3D(empty);
    for(float v:df.data()) if(!std::isinf(v)) throw std::runtime_error("Expected infinite distance");
    for(float v:de.data()) if(v!=0) throw std::runtime_error("Expected zero distance");
    std::cout<<"Distance transform tests passed\n";
}

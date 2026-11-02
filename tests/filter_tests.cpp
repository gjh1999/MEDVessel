#include "filters/Gaussian.hpp"
#include "core/Resample.hpp"
#include "core/Crop.hpp"
#include <iostream>

void near(double actual,double expected,double tolerance=1e-5) {
    if (std::abs(actual-expected)>tolerance || !std::isfinite(actual))
        throw std::runtime_error("Numeric check failed");
}
#include "Test.hpp"
MEDVESSEL_TEST(Filters) {
    using namespace medvessel;
    ImageGeometry g;
    g.spacing={0.5,1,2};
    g.origin={10,-2,5};
    g.direction={0,-1,0,1,0,0,0,0,1};
    Volume3D<float> constant(9,9,9,g,7);
    auto smooth=gaussianFilter3D(constant,1);
    for (auto value:smooth.data()) near(value,7);
    Volume3D<float> impulse(25,25,25,g);
    impulse.at(12,12,12)=1;
    auto response=gaussianFilter3D(impulse,1);
    double mass=0;
    for(auto value:response.data()) mass+=value;
    near(mass,1);
    near(response.at(10,12,12),response.at(12,11,12));
    near(response.at(11,12,12),response.at(13,12,12));
    Volume3D<float> ramp(4,4,4,g);
    for(std::size_t z=0;z<4;++z)
        for(std::size_t y=0;y<4;++y)
            for(std::size_t x=0;x<4;++x) ramp.at(x,y,z)=static_cast<float>(x+2*y+3*z);
    auto same=resampleLinear(ramp,{4,4,4},g);
    for(std::size_t i=0;i<ramp.data().size();++i) near(same.data()[i],ramp.data()[i]);
    auto shifted=g;
    shifted.origin=g.indexToWorld({0.5,0.5,0.5});
    auto sampled=resampleLinear(ramp,{4,4,4},shifted,-100);
    near(sampled.at(0,0,0),3);
    near(sampled.at(2,2,2),15);
    near(sampled.at(3,3,3),-100);
    auto nearest=resampleNearest(ramp,{4,4,4},shifted,-100.0f);
    near(nearest.at(0,0,0),6);near(nearest.at(3,3,3),-100);
    auto cropped=cropVolume(ramp,{1,1,1},{2,2,2});
    near(cropped.at(0,0,0),6);
    auto expectedOrigin=g.indexToWorld({1,1,1});
    for(int a=0;a<3;++a) near(cropped.geometry().origin[a],expectedOrigin[a]);
    bool rejected=false;
    try { gaussianFilter3D(ramp,0); } catch(const std::invalid_argument&) { rejected=true; }
    if(!rejected) throw std::runtime_error("Invalid sigma accepted");
    std::cout<<"Filter and resampling tests passed\n";
}

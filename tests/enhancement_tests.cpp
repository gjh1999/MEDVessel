#include "filters/Frangi.hpp"
#include "segmentation/Nms.hpp"
#include <iostream>
#include <random>

void near(double a,double b,double tolerance=1e-5) {
    if(!std::isfinite(a)||std::abs(a-b)>tolerance) throw std::runtime_error("Numeric check failed");
}
#include "Test.hpp"
MEDVESSEL_TEST(Enhancement) {
    using namespace medvessel;
    std::mt19937 random(42);
    std::uniform_real_distribution<double> distribution(-10,10);
    for(int trial=0;trial<100;++trial) {
        std::array<double,9> a{};
        for(int i=0;i<3;++i) for(int j=i;j<3;++j) a[i*3+j]=a[j*3+i]=distribution(random);
        auto e=eigenSymmetric3x3(a);
        for(int i=0;i<3;++i) {
            for(int row=0;row<3;++row) {
                double value=0;
                for(int col=0;col<3;++col) value+=a[row*3+col]*e.vectors[i][col];
                near(value,e.values[i]*e.vectors[i][row]);
            }
            for(int j=0;j<3;++j) {
                double dot=0;
                for(int k=0;k<3;++k) dot+=e.vectors[i][k]*e.vectors[j][k];
                near(dot,i==j?1:0);
            }
        }
        if(std::abs(e.values[0])>std::abs(e.values[1]) || std::abs(e.values[1])>std::abs(e.values[2]))
            throw std::runtime_error("Incorrect eigenvalue order");
    }
    ImageGeometry g;g.spacing={0.5,1,2};
    Volume3D<float> quadratic(25,17,13,g);
    for(std::size_t z=0;z<13;++z) for(std::size_t y=0;y<17;++y) for(std::size_t x=0;x<25;++x) {
        double px=(static_cast<double>(x)-12)*0.5,py=static_cast<double>(y)-8,pz=(static_cast<double>(z)-6)*2;
        quadratic.at(x,y,z)=static_cast<float>(px*px+2*py*py+3*pz*pz+4*px*py);
    }
    auto h=hessian3D(quadratic,1,false);
    const double expected[6]={2,4,0,4,0,6};
    for(int c=0;c<6;++c) near(h[c].at(12,8,6),expected[c],1e-4);
    Volume3D<float> constant(7,7,7,{},20);
    auto hc=hessian3D(constant,1);
    for(const auto& component:hc) for(float v:component.data()) near(v,0);
    Volume3D<float> tube(25,25,9);
    for(std::size_t z=0;z<9;++z) for(std::size_t y=0;y<25;++y) for(std::size_t x=0;x<25;++x) {
        double dx=static_cast<double>(x)-12,dy=static_cast<double>(y)-12;
        tube.at(x,y,z)=static_cast<float>(300*std::exp(-(dx*dx+dy*dy)/8));
    }
    FrangiOptions options;options.scalesMm={1,2,3};options.c=10;
    auto bright=frangi3D(tube,options);
    if(bright.vesselness.at(12,12,4)<0.5) throw std::runtime_error("Tube not enhanced");
    near(bright.bestScaleMm.at(12,12,4),2);
    auto nms=nonMaximumSuppression3D(tube,bright,options,1);
    near(nms.at(12,12,4),bright.vesselness.at(12,12,4));
    near(nms.at(13,12,4),0);
    options.brightVessels=false;
    auto wrongSign=frangi3D(tube,options);
    near(wrongSign.vesselness.at(12,12,4),0);
    for(std::size_t z=0;z<9;++z) for(std::size_t y=0;y<25;++y) for(std::size_t x=0;x<25;++x)
        tube.at(x,y,z)=-tube.at(x,y,z);
    auto dark=frangi3D(tube,options);
    near(dark.vesselness.at(12,12,4),bright.vesselness.at(12,12,4));
    std::cout<<"Enhancement tests passed\n";
}

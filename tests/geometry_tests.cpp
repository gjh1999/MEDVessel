#include "geometry/BranchAnalysis.hpp"
#include "geometry/CrossSection.hpp"
#include <iostream>
void near(double a,double b) {if(std::abs(a-b)>1e-8) throw std::runtime_error("Geometry check failed");}
#include "Test.hpp"
MEDVESSEL_TEST(Geometry) {
    using namespace medvessel;
    near(polylineLengthMm({{0,0,0},{3,4,0},{3,4,12}}),17);
    std::vector<Point3D> x{{10,20,30},{11,20,30},{12,20,30},{12,20,30}};
    std::vector<Point3D> y{{10,20,30},{10,22,30},{10,25,30}};
    near(branchAngleDegrees(x,y,1.5),90);
    near(branchAngleDegrees(x,x,20),0);
    auto d=branchDirection(x,0.25);near(d[0],1);near(d[1],0);
    bool rejected=false;
    try {branchDirection({{0,0,0},{0,0,0}},1);} catch(const std::invalid_argument&) {rejected=true;}
    if(!rejected) throw std::runtime_error("Degenerate branch accepted");
    Volume3D<std::uint8_t> box(15,15,9);
    for(std::size_t z=0;z<9;++z) for(std::size_t y=5;y<=9;++y) for(std::size_t x=5;x<=9;++x)
        box.at(x,y,z)=1;
    auto section=crossSection(box,{7,7,4},{0,0,1},1,5);
    near(section.areaMm2,25);near(section.equivalentDiameterMm,2*std::sqrt(25/std::acos(-1.0)));
    box.at(1,1,4)=1; // A disconnected vessel in the same plane is excluded.
    near(crossSection(box,{7,7,4},{0,0,1},1,7).areaMm2,25);
    auto straight=polylineCurvature({{0,0,0},{1,0,0},{2,0,0}},1);near(straight[1],0);
    std::vector<Point3D> circle;
    for(int i=0;i<10;++i) {double t=i*0.1;circle.push_back({5*std::cos(t),5*std::sin(t),0});}
    auto curvature=polylineCurvature(circle);
    for(std::size_t i=1;i+1<circle.size();++i) near(curvature[i],0.2);
    std::cout<<"Geometry tests passed\n";
}

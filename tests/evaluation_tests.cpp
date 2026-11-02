#include "geometry/Evaluation.hpp"
#include <iostream>
void near(double a,double b) {if(std::abs(a-b)>1e-5) throw std::runtime_error("Evaluation check failed");}
#include "Test.hpp"
MEDVESSEL_TEST(Evaluation) {
    using namespace medvessel;
    ImageGeometry g;g.spacing={0.5,1,2};
    Volume3D<std::uint8_t> a(5,5,5,g),b=a,empty=a;
    a.at(1,2,2)=1;b.at(3,2,2)=1;
    near(hausdorff95Mm(a,b),1);near(hausdorff95Mm(a,a),0);near(hausdorff95Mm(empty,empty),0);
    if(!std::isinf(hausdorff95Mm(a,empty))) throw std::runtime_error("Expected infinite HD95");
    near(clDice(a,a,a,a),1);near(clDice(a,b,a,b),0);near(clDice(empty,empty,empty,empty),1);
    std::cout<<"Evaluation tests passed\n";
}

#include "core/Volume3D.hpp"
#include <iostream>

void require(bool condition) {
    if (!condition) throw std::runtime_error("Test failed");
}
template<class Exception, class Function>
void rejects(Function function) {
    try { function(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("Expected exception missing");
}
#include "Test.hpp"
MEDVESSEL_TEST(Core) {
    using namespace medvessel;
    Volume3D<int> volume(3, 4, 2);
    volume.at(2, 1, 1) = 42;
    require(volume.data()[17] == 42);
    rejects<std::out_of_range>([&] { volume.at(3, 0, 0); });
    rejects<std::invalid_argument>([] { Volume3D<int> empty(0, 1, 1); });
    rejects<std::overflow_error>([] {
        Volume3D<int> huge(std::numeric_limits<std::size_t>::max(), 2, 2);
    });
    ImageGeometry geometry;
    geometry.spacing = {0.5, 0.75, 2};
    geometry.origin = {10, -20, 30};
    geometry.direction = {0,-1,0, 1,0,0, 0,0,-1};
    auto world = geometry.indexToWorld({2, 4, 3});
    require(world == Point3D{7, -19, 24});
    auto index = geometry.worldToIndex(world);
    require(index == Point3D{2, 4, 3});
    geometry.spacing[0] = 0;
    rejects<std::invalid_argument>([&] { geometry.validate(); });
    geometry.spacing[0] = 1;
    geometry.direction[0] = 2;
    rejects<std::invalid_argument>([&] { geometry.validate(); });
    std::cout << "Core tests passed\n";
}

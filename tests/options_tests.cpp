#include "io/RunOptions.hpp"
#include <iostream>
#include "Test.hpp"
MEDVESSEL_TEST(Options) {
    using namespace medvessel;
    auto o=parseRunOptions({"--scales","1,2,3","--c","20","--dark","--seed","1,2,3","--connectivity","6"});
    if(o.frangi.scalesMm.size()!=3||o.frangi.c!=20||o.frangi.brightVessels||o.seeds[0]!=VoxelIndex{1,2,3}||o.segmentation.connectivity!=6)
        throw std::runtime_error("Options not parsed");
    for(const auto& arguments:std::vector<std::vector<std::string>>{
        {"--scales","1,"},{"--seed","-1,2,3"},{"--seed","1,2,3,4"},{"--c","nan"},
        {"--low","0.8","--high","0.1"},{"--connectivity","7"},{"--c"},{"--unknown","3"},
        {"--min-component","1.5"},{"--prune-mm","-1"},{"--nms-step","0"}}) {
        bool rejected=false;try {parseRunOptions(arguments);} catch(const std::exception&) {rejected=true;}
        if(!rejected) throw std::runtime_error("Invalid options accepted");
    }
    if(jsonString("a\n\"\\")!="\"a\\u000a\\\"\\\\\"") throw std::runtime_error("JSON escaping failed");
    auto roi=parseRunOptions({"--roi","1,2,3,4,5,6","--min-intensity","1600"});
    if(!roi.roi||(*roi.roi)[5]!=6||roi.segmentation.minIntensity!=1600) throw std::runtime_error("ROI parse failed");
    std::cout<<"Options tests passed\n";
}

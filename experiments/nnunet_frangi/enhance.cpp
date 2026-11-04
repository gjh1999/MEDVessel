#include "io/Nifti.hpp"
#include "io/RunOptions.hpp"
#include "filters/Frangi.hpp"
#include <filesystem>
#include <iostream>

// Image-only preparation: deliberately accepts no labels, seeds or segmentation options.
int main(int argc, char** argv) {
    if(argc < 3) {
        std::cerr << "Usage: medvessel_enhance input.nii.gz vesselness.nii.gz [--scales mm,... --alpha n --beta n --c n]\n";
        return 2;
    }
    try {
        for(int i=3;i<argc;i+=2) {
            const std::string key=argv[i];
            if(key!="--scales"&&key!="--alpha"&&key!="--beta"&&key!="--c")
                throw std::invalid_argument("Only enhancement parameters are accepted");
        }
        const auto options=medvessel::parseRunOptions(std::vector<std::string>(argv+3,argv+argc));
        if(std::filesystem::weakly_canonical(argv[1])==std::filesystem::weakly_canonical(argv[2]))
            throw std::invalid_argument("Output cannot overwrite input");
        if(std::filesystem::exists(argv[2])) throw std::invalid_argument("Output already exists");
        auto input=medvessel::readNifti(argv[1]);
        auto enhanced=medvessel::frangi3D(input,options.frangi);
        const auto parent=std::filesystem::path(argv[2]).parent_path();
        if(!parent.empty()) std::filesystem::create_directories(parent);
        medvessel::writeNifti(enhanced.vesselness,argv[2]);
        std::cout << "Wrote image-only Frangi channel; no labels or seeds read.\n";
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

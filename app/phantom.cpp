#include "io/Nifti.hpp"
#include "core/Phantom.hpp"
#include <iostream>
#include <filesystem>
int main(int argc,char** argv) {
    if(argc<2||argc>3) {std::cerr<<"Usage: medvessel_phantom output_directory [straight|curved|y|noisy]\n";return 2;}
    try {
        std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
        medvessel::ImageGeometry g;g.spacing={0.5,0.5,1};g.origin={-8,-8,-8};
        std::string scenario=argc==3?argv[2]:"straight";
        if(scenario!="straight") {
            g.origin={-12,-12,-12};g.spacing={0.6,0.6,1.2};
            std::vector<std::vector<medvessel::Point3D>> branches;
            double noise=0;
            if(scenario=="y") branches={{{0,0,-10},{0,0,0}},{{0,0,0},{-7,0,9}},{{0,0,0},{7,0,9}}};
            else if(scenario=="curved") {
                std::vector<medvessel::Point3D> arc;
                for(int i=0;i<=40;++i) {double t=-1.2+i*2.4/40;arc.push_back({8*std::cos(t)-4,0,8*std::sin(t)});}
                branches.push_back(arc);
            } else if(scenario=="noisy") {branches={{{-6,-4,-9},{6,4,9}}};noise=10;}
            else throw std::invalid_argument("Unknown phantom scenario");
            auto phantom=medvessel::tubePhantom({41,41,21},g,branches,2,noise);
            medvessel::writeNifti(phantom.source,(directory/"phantom.nii.gz").string());
            medvessel::writeNifti(phantom.reference,(directory/"reference_mask.nii.gz").string());
            std::cout<<"Wrote "<<scenario<<" phantom, radius=2 mm, noise_stddev="<<noise<<"\n";
            return 0;
        }
        medvessel::Volume3D<float> volume(33,33,17,g),truth(33,33,17,g);
        for(std::size_t z=0;z<17;++z) for(std::size_t y=0;y<33;++y) for(std::size_t x=0;x<33;++x) {
            double dx=(static_cast<double>(x)-16)*0.5,dy=(static_cast<double>(y)-16)*0.5;
            volume.at(x,y,z)=static_cast<float>(300*std::exp(-(dx*dx+dy*dy)/8));
            truth.at(x,y,z)=dx*dx+dy*dy<=4?1.0f:0.0f;
        }
        medvessel::writeNifti(volume,(directory/"phantom.nii.gz").string());
        medvessel::writeNifti(truth,(directory/"reference_mask.nii.gz").string());
        std::cout<<"Wrote anisotropic Gaussian-profile cylinder; reference radius=2 mm.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#include "io/Nifti.hpp"
#include "segmentation/RegionGrowing.hpp"
#include "skeleton/CenterlineGraph.hpp"
#include "geometry/CrossSection.hpp"
#include <filesystem>
#include <iostream>
void require(bool v) {if(!v) throw std::runtime_error("Pipeline output check failed");}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    using namespace medvessel;std::filesystem::path root(argv[1]);
    auto mask=readNifti((root/"output/mask.nii.gz").string());
    auto skeleton=readNifti((root/"output/skeleton.nii.gz").string());
    auto truth=readNifti((root/"input/reference_mask.nii.gz").string());
    auto best=readNifti((root/"output/best_scale_mm.nii.gz").string());
    auto d=mask.dimensions();
    Volume3D<std::uint8_t> binary(d[0],d[1],d[2],mask.geometry()),reference=binary,skel=binary;
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x) {
        require(mask.at(x,y,z)==0||mask.at(x,y,z)==1);
        require(skeleton.at(x,y,z)<=mask.at(x,y,z));
        binary.at(x,y,z)=mask.at(x,y,z)!=0;reference.at(x,y,z)=truth.at(x,y,z)!=0;
        skel.at(x,y,z)=skeleton.at(x,y,z)!=0;
    }
    double overlap=dice(binary,reference);require(overlap>0.6);
    require(best.at(16,16,8)==2);
    auto graph=centerlineGraph(skel);require(graph.nodes.size()==2&&graph.branches.size()==1);
    require(std::abs(graph.branches[0].lengthMm-16)<1e-5);
    auto section=crossSection(binary,{0,0,0},{0,0,1},0.25,24);
    require(section.equivalentDiameterMm>2&&section.equivalentDiameterMm<5);
    std::cout<<"Pipeline Dice="<<overlap<<", length="<<graph.branches[0].lengthMm
             <<" mm, section diameter="<<section.equivalentDiameterMm<<" mm\n";
}

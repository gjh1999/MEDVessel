#include "io/Nifti.hpp"
#include "geometry/Evaluation.hpp"
#include "skeleton/CenterlineGraph.hpp"
#include "segmentation/ConnectedComponents.hpp"
#include <iostream>
#include <filesystem>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    using namespace medvessel;std::filesystem::path root(argv[1]);
    auto readBinary=[](const std::filesystem::path& path) {
        auto image=readNifti(path.string());auto d=image.dimensions();
        Volume3D<std::uint8_t> result(d[0],d[1],d[2],image.geometry());
        for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
            result.at(x,y,z)=image.at(x,y,z)>0;
        return result;
    };
    auto mask=readBinary(root/"output/mask.nii.gz"),truth=readBinary(root/"reference_mask.nii.gz");
    auto skeleton=readBinary(root/"output/skeleton.nii.gz");auto graph=centerlineGraph(skeleton);
    auto expected=static_cast<std::size_t>(std::stoul(argv[2]));
    if(graph.branches.size()!=expected||connectedComponents3D(mask).counts.size()!=2||
       dice(mask,truth)<0.8||hausdorff95Mm(mask,truth)>1.5)
        throw std::runtime_error("Synthetic scenario regression");
    std::cout<<"Scenario Dice="<<dice(mask,truth)<<", branches="<<graph.branches.size()<<'\n';
}

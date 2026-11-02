#include "io/Nifti.hpp"
#include "io/RunOptions.hpp"
#include "core/Crop.hpp"
#include "skeleton/DistanceTransform.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

// Reference-guided development crop, explicitly unsuitable for blind benchmarking.
int main(int argc,char** argv) {
    if(argc!=4&&argc!=5) {std::cerr<<"Usage: medvessel_prepare_case source.nii.gz label.nii.gz output_directory [half_width_mm]\n";return 2;}
    try {
        double halfMm=argc==5?medvessel::parseNumber(argv[4]):20;
        if(halfMm<=0) throw std::invalid_argument("ROI half-width must be positive");
        auto source=medvessel::readNifti(argv[1]),label=medvessel::readNifti(argv[2]);
        auto d=source.dimensions();
        if(d!=label.dimensions()||source.geometry().spacing!=label.geometry().spacing||
           source.geometry().origin!=label.geometry().origin||source.geometry().direction!=label.geometry().direction)
            throw std::invalid_argument("Source and reference grids differ");
        medvessel::VoxelIndex first=d,last{};bool found=false;
        for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
            if(label.at(x,y,z)>0) {
                medvessel::VoxelIndex p{x,y,z};found=true;
                for(int a=0;a<3;++a) {first[a]=std::min(first[a],p[a]);last[a]=std::max(last[a],p[a]);}
            }
        if(!found) throw std::invalid_argument("Reference contains no foreground");
        // Add one background voxel around the reference bbox when available.
        medvessel::VoxelIndex bounds{};
        for(int a=0;a<3;++a) {if(first[a]>0) --first[a];last[a]=std::min(last[a]+1,d[a]-1);bounds[a]=last[a]-first[a]+1;}
        auto bounded=medvessel::cropVolume(label,first,bounds);
        medvessel::Volume3D<std::uint8_t> binary(bounds[0],bounds[1],bounds[2],bounded.geometry());
        for(std::size_t z=0;z<bounds[2];++z) for(std::size_t y=0;y<bounds[1];++y) for(std::size_t x=0;x<bounds[0];++x)
            binary.at(x,y,z)=bounded.at(x,y,z)>0;
        auto distance=medvessel::distanceTransform3D(binary);
        float best=0;medvessel::VoxelIndex seed{};
        for(std::size_t z=0;z<bounds[2];++z) for(std::size_t y=0;y<bounds[1];++y) for(std::size_t x=0;x<bounds[0];++x)
            if(std::isfinite(distance.at(x,y,z))&&distance.at(x,y,z)>best) {best=distance.at(x,y,z);seed={x+first[0],y+first[1],z+first[2]};}
        if(best<=0) throw std::invalid_argument("No finite interior reference seed");
        medvessel::VoxelIndex start{},size{},localSeed{};
        for(int a=0;a<3;++a) {
            double radius=std::ceil(halfMm/source.geometry().spacing[a]);
            if(radius>static_cast<double>(d[a])) radius=static_cast<double>(d[a]);
            auto half=static_cast<std::size_t>(radius);
            start[a]=seed[a]>half?seed[a]-half:0;
            auto end=seed[a]+std::min(half,d[a]-1-seed[a]);
            size[a]=end-start[a]+1;localSeed[a]=seed[a]-start[a];
        }
        auto sourceCrop=medvessel::cropVolume(source,start,size),referenceCrop=medvessel::cropVolume(label,start,size);
        std::filesystem::path output(argv[3]);std::filesystem::create_directories(output);
        medvessel::writeNifti(sourceCrop,(output/"source.nii.gz").string());
        medvessel::writeNifti(referenceCrop,(output/"reference.nii.gz").string());
        std::ofstream manifest(output/"roi.json");
        manifest<<"{\n  \"reference_guided_development_only\": true,\n  \"half_width_mm\": "<<halfMm
                <<",\n  \"source_roi_voxel\": ["<<start[0]<<','<<start[1]<<','<<start[2]<<','<<size[0]<<','<<size[1]<<','<<size[2]
                <<"],\n  \"seed_voxel\": ["<<localSeed[0]<<','<<localSeed[1]<<','<<localSeed[2]
                <<"],\n  \"reference_seed_radius_mm\": "<<best<<"\n}\n";
        manifest.close();if(!manifest) throw std::runtime_error("Cannot write reference ROI metadata");
        std::cout<<"Reference-guided ROI: "<<size[0]<<'x'<<size[1]<<'x'<<size[2]
                 <<"; seed="<<localSeed[0]<<','<<localSeed[1]<<','<<localSeed[2]<<" (development only)\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

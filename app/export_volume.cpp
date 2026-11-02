#include "io/Nifti.hpp"
#include "io/RunOptions.hpp"
#include "core/Crop.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
int main(int argc,char** argv) {
    if(argc!=3&&argc!=5) {std::cerr<<"Usage: medvessel_export input.nii.gz output_prefix [--roi x,y,z,w,h,d]\n";return 2;}
    try {
        auto volume=medvessel::readNifti(argv[1]);
        if(argc==5) {
            if(std::string(argv[3])!="--roi") throw std::invalid_argument("Unknown export option");
            auto r=medvessel::parseIndices<6>(argv[4]);
            volume=medvessel::cropVolume(volume,{r[0],r[1],r[2]},{r[3],r[4],r[5]});
        }
        std::filesystem::path prefix(argv[2]);if(!prefix.parent_path().empty()) std::filesystem::create_directories(prefix.parent_path());
        std::ofstream raw(prefix.string()+".raw",std::ios::binary);
        const std::uint16_t one=1;bool little=*reinterpret_cast<const unsigned char*>(&one)==1;
        for(float v:volume.data()) {
            const auto* bytes=reinterpret_cast<const char*>(&v);
            if(little) raw.write(bytes,4);else for(int i=3;i>=0;--i) raw.put(bytes[i]);
        }
        auto d=volume.dimensions();const auto& g=volume.geometry();
        std::ofstream metadata(prefix.string()+".json");metadata<<std::setprecision(17);
        metadata<<"{\n  \"dtype\": \"float32_le\",\n  \"raw_file\": "<<medvessel::jsonString(prefix.filename().string()+".raw")
                <<",\n  \"dimensions_xyz\": ["<<d[0]<<','<<d[1]<<','<<d[2]<<"],\n  \"spacing_mm\": ["
                <<g.spacing[0]<<','<<g.spacing[1]<<','<<g.spacing[2]<<"],\n  \"origin_ras_mm\": ["
                <<g.origin[0]<<','<<g.origin[1]<<','<<g.origin[2]<<"],\n  \"direction\": [";
        for(std::size_t i=0;i<9;++i) metadata<<(i?",":"")<<g.direction[i];
        metadata<<"]\n}\n";metadata.close();raw.close();
        if(!raw||!metadata) throw std::runtime_error("Cannot write volume export");
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#include "io/Nifti.hpp"
#include "io/RunOptions.hpp"
#include "core/Resample.hpp"
#include "geometry/Evaluation.hpp"
#include "skeleton/Thinning.hpp"
#include "segmentation/ConnectedComponents.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
int main(int argc,char** argv) {
    if(argc<4) {std::cerr<<"Usage: medvessel_evaluate prediction.nii.gz reference.nii.gz metrics.json [--reference-levelset-negative] [--align-reference]\n";return 2;}
    try {
        bool negativeInside=false,alignReference=false;
        for(int i=4;i<argc;++i) {
            if(std::string(argv[i])=="--reference-levelset-negative") negativeInside=true;
            else if(std::string(argv[i])=="--align-reference") alignReference=true;
            else throw std::invalid_argument("Unknown reference option");
        }
        auto prediction=medvessel::readNifti(argv[1]),reference=medvessel::readNifti(argv[2]);
        if(alignReference) reference=medvessel::resampleNearest(reference,prediction.dimensions(),prediction.geometry(),negativeInside?1.0f:0.0f);
        auto binary=[](const medvessel::Volume3D<float>& image,bool negative) {
            auto d=image.dimensions();medvessel::Volume3D<std::uint8_t> result(d[0],d[1],d[2],image.geometry());
            for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
                result.at(x,y,z)=negative?image.at(x,y,z)<0:image.at(x,y,z)>0;
            return result;
        };
        auto a=binary(prediction,false),b=binary(reference,negativeInside);
        double overlap=medvessel::dice(a,b),hd=medvessel::hausdorff95Mm(a,b);
        auto sa=medvessel::thinning3D(a),sb=medvessel::thinning3D(b);
        double topology=medvessel::clDice(a,b,sa,sb);
        auto ab=medvessel::directedVoxelDistances(sa,sb),ba=medvessel::directedVoxelDistances(sb,sa);
        double sum=0;for(double v:ab) sum+=v;for(double v:ba) sum+=v;
        double centreDistance=ab.empty()&&ba.empty()?0:
            ab.empty()||ba.empty()?std::numeric_limits<double>::infinity():sum/(ab.size()+ba.size());
        auto ca=medvessel::connectedComponents3D(a).counts.size()-1,cb=medvessel::connectedComponents3D(b).counts.size()-1;
        std::ofstream out(argv[3]);out<<std::setprecision(12);
        out<<"{\n  \"prediction_path\": "<<medvessel::jsonString(argv[1])
           <<",\n  \"reference_path\": "<<medvessel::jsonString(argv[2])
           <<",\n  \"reference_negative_inside\": "<<(negativeInside?"true":"false")
           <<",\n  \"reference_aligned_to_prediction\": "<<(alignReference?"true":"false")
           <<",\n  \"dice\": "<<overlap<<",\n  \"cldice\": "<<topology<<",\n  \"hd95_mm\": ";
        if(std::isfinite(hd)) out<<hd;else out<<"null";
        out<<",\n  \"symmetric_mean_centerline_distance_mm\": ";
        if(std::isfinite(centreDistance)) out<<centreDistance;else out<<"null";
        out<<",\n  \"prediction_components\": "<<ca<<",\n  \"reference_components\": "<<cb<<"\n}\n";
        out.close();if(!out) throw std::runtime_error("Cannot write metrics");
        std::cout<<"Dice="<<overlap<<", clDice="<<topology<<", HD95="<<hd<<" mm\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

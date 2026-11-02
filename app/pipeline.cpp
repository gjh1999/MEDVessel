#include "io/Nifti.hpp"
#include "io/RunOptions.hpp"
#include "io/GeometryReports.hpp"
#include "io/Preview.hpp"
#include "segmentation/ConnectedComponents.hpp"
#include "segmentation/Nms.hpp"
#include "skeleton/Prune.hpp"
#include "core/Crop.hpp"
#include "filters/Frangi.hpp"
#include "segmentation/RegionGrowing.hpp"
#include "skeleton/DistanceTransform.hpp"
#include "skeleton/Thinning.hpp"
#include "skeleton/CenterlineGraph.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef MEDVESSEL_HAVE_DICOM
#include "io/DicomSeries.hpp"
#endif

int main(int argc,char** argv) {
    if(argc<3) {
        std::cerr<<"Usage: medvessel_cli input.nii[.gz] output_directory [options]\n"
                 <<"Options: --scales mm,mm,... --alpha n --beta n --c n --dark\n"
                 <<"         --low n --high n --min-intensity n --max-intensity n --connectivity 6|26\n"
                 <<"         --roi x,y,z,width,height,depth --seed x,y,z (indices AFTER ROI)\n";
        std::cerr<<"         --nms-seeds --nms-step mm --min-component voxels --prune-mm mm\n";
        return 2;
    }
    try {
        auto stage=[](const char* name) {std::cerr<<"MEDVESSEL_STAGE\t"<<name<<std::endl;};
        auto start=std::chrono::steady_clock::now();
        auto run=medvessel::parseRunOptions(std::vector<std::string>(argv+3,argv+argc));
        auto loadSource=[&] {
            if(std::filesystem::is_directory(argv[1])) {
                #ifdef MEDVESSEL_HAVE_DICOM
                return medvessel::readDicomSeries(argv[1]);
                #else
                throw std::invalid_argument("DICOM support was not enabled at build time");
                #endif
            }
            return medvessel::readNifti(argv[1]);
        };
        stage("read");
        auto source=loadSource();
        if(run.roi) {
            auto r=*run.roi;source=medvessel::cropVolume(source,{r[0],r[1],r[2]},{r[3],r[4],r[5]});
        }
        std::filesystem::path output(argv[2]);std::filesystem::create_directories(output);
        if(std::filesystem::is_regular_file(argv[1])) {
            const auto inputPath=std::filesystem::weakly_canonical(argv[1]);
            for(const char* name:{"vesselness.nii.gz","best_scale_mm.nii.gz","mask.nii.gz","skeleton.nii.gz", "radius_field_mm.nii.gz",
                                  "branches.csv","centerline.csv","sections.csv","branch_angles.csv","nodes.csv","preview.svg","run.json"})
                if(inputPath==std::filesystem::weakly_canonical(output/name))
                    throw std::invalid_argument("Output directory would overwrite input image");
        }
        stage("enhance");
        auto enhanced=medvessel::frangi3D(source,run.frangi);
        const bool explicitSeedMode=run.nmsSeeds||!run.seeds.empty();
        if(run.nmsSeeds&&run.seeds.empty()) {
            stage("nms");
            auto nms=medvessel::nonMaximumSuppression3D(source,enhanced,run.frangi,run.nmsStepMm);
            auto shape=source.dimensions();
            for(std::size_t z=0;z<shape[2];++z) for(std::size_t y=0;y<shape[1];++y) for(std::size_t x=0;x<shape[0];++x)
                if(nms.at(x,y,z)>0&&nms.at(x,y,z)>=run.segmentation.highThreshold) run.seeds.push_back({x,y,z});
        }
        stage("segment");
        auto mask=explicitSeedMode?medvessel::seededRegionGrowing3D(enhanced.vesselness,source,run.seeds,run.segmentation)
            :medvessel::hysteresisThreshold3D(enhanced.vesselness,source,run.segmentation);
        if(run.minComponentVoxels) mask=medvessel::removeSmallComponents(mask,run.minComponentVoxels,run.segmentation.connectivity);
        stage("distance");
        auto distance=medvessel::distanceTransform3D(mask);
        stage("skeleton");
        auto skeleton=medvessel::thinning3D(mask);
        if(run.pruneLengthMm>0) skeleton=medvessel::pruneShortBranches(skeleton,run.pruneLengthMm);
        stage("graph");
        auto graph=medvessel::centerlineGraph(skeleton);
        stage("reports");
        medvessel::writeGeometryReports(graph,mask,distance,output);
        medvessel::writePreviewSvg(source,enhanced.vesselness,mask,graph,output/"preview.svg");
        auto d=source.dimensions();
        medvessel::Volume3D<float> floatMask(d[0],d[1],d[2],source.geometry());
        for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
            floatMask.at(x,y,z)=mask.at(x,y,z);
        stage("write");
        medvessel::writeNifti(enhanced.vesselness,(output/"vesselness.nii.gz").string());
        medvessel::writeNifti(enhanced.bestScaleMm,(output/"best_scale_mm.nii.gz").string());
        medvessel::writeNifti(floatMask,(output/"mask.nii.gz").string());
        for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x)
            floatMask.at(x,y,z)=skeleton.at(x,y,z);
        medvessel::writeNifti(floatMask,(output/"skeleton.nii.gz").string());
        // All-foreground masks have no finite in-volume EDT reference.
        bool finiteDistance=true;for(float v:distance.data()) if(!std::isfinite(v)) finiteDistance=false;
        if(finiteDistance) medvessel::writeNifti(distance,(output/"radius_field_mm.nii.gz").string());
        else std::filesystem::remove(output/"radius_field_mm.nii.gz"); // Remove only this run's obsolete derived field.
        std::ofstream report(output/"branches.csv");
        report<<"branch_id,start_node_id,end_node_id,length_mm,mean_edt_diameter_mm\n";
        std::ofstream points(output/"centerline.csv");
        points<<"branch_id,point_id,x_ras_mm,y_ras_mm,z_ras_mm\n";
        for(std::size_t i=0;i<graph.branches.size();++i) {
            const auto& branch=graph.branches[i];double diameter=0;
            for(auto p:branch.voxels) diameter+=2*distance.at(p[0],p[1],p[2])/branch.voxels.size();
            report<<i<<','<<branch.startNodeId<<','<<branch.endNodeId<<','<<branch.lengthMm<<',';
            if(std::isfinite(diameter)) report<<diameter;
            report<<'\n';
            for(std::size_t j=0;j<branch.centerlineWorld.size();++j) {
                auto p=branch.centerlineWorld[j];points<<i<<','<<j<<','<<p[0]<<','<<p[1]<<','<<p[2]<<'\n';
            }
        }
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::ofstream metadata(output/"run.json");
        metadata<<"{\n  \"schema_version\": 1,\n  \"coordinate_system\": \"RAS_mm\",\n"
                <<"  \"input_path\": "<<medvessel::jsonString(std::filesystem::absolute(argv[1]).string())<<",\n"
                <<"  \"input_kind\": "<<(std::filesystem::is_directory(argv[1])?"\"dicom_series\"":"\"nifti\"")<<",\n"
                <<"  \"input_bytes\": ";
        if(std::filesystem::is_regular_file(argv[1])) metadata<<std::filesystem::file_size(argv[1]);else metadata<<"null";
        metadata<<",\n  \"roi_voxel\": ";
        if(run.roi) {
            metadata<<'[';for(std::size_t i=0;i<6;++i) metadata<<(i?",":"")<<(*run.roi)[i];metadata<<']';
        } else metadata<<"null";
        metadata<<",\n  \"dimensions\": ["<<d[0]<<','<<d[1]<<','<<d[2]<<"],\n  \"spacing_mm\": ["
                <<source.geometry().spacing[0]<<','<<source.geometry().spacing[1]<<','<<source.geometry().spacing[2]<<"],\n"
                <<"  \"scales_mm\": [";
        for(std::size_t i=0;i<run.frangi.scalesMm.size();++i) metadata<<(i?", ":"")<<run.frangi.scalesMm[i];
        metadata<<"],\n  \"alpha\": "<<run.frangi.alpha<<", \"beta\": "<<run.frangi.beta<<", \"c\": "<<run.frangi.c<<",\n"
                <<"  \"bright_vessels\": "<<(run.frangi.brightVessels?"true":"false")<<",\n"
                <<"  \"low_threshold\": "<<run.segmentation.lowThreshold<<", \"high_threshold\": "<<run.segmentation.highThreshold<<",\n"
                <<"  \"min_intensity\": ";
        if(std::isfinite(run.segmentation.minIntensity)) metadata<<run.segmentation.minIntensity;else metadata<<"null";
        metadata<<", \"max_intensity\": ";
        if(std::isfinite(run.segmentation.maxIntensity)) metadata<<run.segmentation.maxIntensity;else metadata<<"null";
        metadata<<",\n  \"seeds_voxel\": [";
        for(std::size_t i=0;i<run.seeds.size();++i) {
            auto p=run.seeds[i];metadata<<(i?",":"")<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';
        }
        int maxThreads=1;
        #ifdef _OPENMP
        maxThreads=omp_get_max_threads();
        #endif
        metadata<<"],\n  \"max_threads\": "<<maxThreads
                <<",\n  \"nms_seeds\": "<<(run.nmsSeeds?"true":"false")<<", \"nms_step_mm\": "<<run.nmsStepMm
                <<",\n  \"minimum_component_voxels\": "<<run.minComponentVoxels<<", \"prune_length_mm\": "<<run.pruneLengthMm
                <<",\n  \"connectivity\": "<<run.segmentation.connectivity<<",\n  \"nodes\": "<<graph.nodes.size()<<",\n"
                <<"  \"branches\": "<<graph.branches.size()<<",\n  \"elapsed_seconds\": "<<seconds<<"\n}\n";
        report.close();points.close();metadata.close();
        if(!report||!points||!metadata) throw std::runtime_error("Cannot write output reports");
        std::cout<<"Wrote enhancement, mask, skeleton and branch reports; "<<graph.branches.size()
                 <<" branches, "<<seconds<<" seconds.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

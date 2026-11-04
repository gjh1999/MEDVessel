#pragma once
#include "filters/Frangi.hpp"
#include "segmentation/RegionGrowing.hpp"
#include <sstream>
#include <string>
#include <optional>

namespace medvessel {
struct RunOptions {
    FrangiOptions frangi;
    RegionGrowingOptions segmentation;
    std::vector<VoxelIndex> seeds;
    bool nmsSeeds=false;
    double nmsStepMm=1,pruneLengthMm=0;
    double growthBandMm=0,seedCoreRatio=.35;
    std::size_t minComponentVoxels=0;
    std::optional<std::array<std::size_t,6>> roi;
};
template<std::size_t N>
std::array<std::size_t,N> parseIndices(const std::string& value) {
    std::istringstream stream(value);std::string item;std::array<std::size_t,N> indices{};
    for(std::size_t a=0;a<N;++a) {
        if(!std::getline(stream,item,',')||item.empty()||item[0]=='-') throw std::invalid_argument("Invalid index list");
        std::size_t consumed=0;auto index=std::stoull(item,&consumed);
        if(consumed!=item.size()||index>std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("Invalid index");
        indices[a]=static_cast<std::size_t>(index);
    }
    if(std::getline(stream,item,',')||value.back()==',') throw std::invalid_argument("Incorrect number of indices");
    return indices;
}
inline double parseNumber(const std::string& text) {
    std::size_t consumed=0;double value=std::stod(text,&consumed);
    if(consumed!=text.size()||!std::isfinite(value)) throw std::invalid_argument("Invalid numeric value: "+text);
    return value;
}
inline RunOptions parseRunOptions(const std::vector<std::string>& arguments) {
    RunOptions result;
    for(std::size_t i=0;i<arguments.size();++i) {
        const auto& key=arguments[i];
        if(key=="--dark") {result.frangi.brightVessels=false;continue;}
        if(key=="--nms-seeds") {result.nmsSeeds=true;continue;}
        if(i+1==arguments.size()) throw std::invalid_argument("Missing value for "+key);
        const auto& value=arguments[++i];
        if(key=="--scales") {
            result.frangi.scalesMm.clear();std::istringstream stream(value);std::string item;
            if(value.empty()||value.back()==',') throw std::invalid_argument("Invalid scale list");
            while(std::getline(stream,item,',')) {
                double scale=parseNumber(item);if(scale<=0) throw std::invalid_argument("Scale must be positive");
                result.frangi.scalesMm.push_back(scale);
            }
        } else if(key=="--seed") {
            result.seeds.push_back(parseIndices<3>(value));
        } else if(key=="--roi") {
            result.roi=parseIndices<6>(value);
        } else {
            double number=parseNumber(value);
            if(key=="--alpha") result.frangi.alpha=number;
            else if(key=="--beta") result.frangi.beta=number;
            else if(key=="--c") result.frangi.c=number;
            else if(key=="--nms-step") result.nmsStepMm=number;
            else if(key=="--prune-mm") result.pruneLengthMm=number;
            else if(key=="--growth-band-mm") result.growthBandMm=number;
            else if(key=="--seed-core-ratio") result.seedCoreRatio=number;
            else if(key=="--min-component") {
                if(number<0||number!=std::floor(number)||number>=static_cast<double>(std::numeric_limits<std::size_t>::max()))
                    throw std::invalid_argument("Invalid component size");
                result.minComponentVoxels=static_cast<std::size_t>(number);
            }
            else if(key=="--low") result.segmentation.lowThreshold=static_cast<float>(number);
            else if(key=="--high") result.segmentation.highThreshold=static_cast<float>(number);
            else if(key=="--min-hu"||key=="--min-intensity") result.segmentation.minIntensity=static_cast<float>(number);
            else if(key=="--max-hu"||key=="--max-intensity") result.segmentation.maxIntensity=static_cast<float>(number);
            else if(key=="--connectivity") {
                if(number!=6&&number!=26) throw std::invalid_argument("Connectivity must be 6 or 26");
                result.segmentation.connectivity=static_cast<int>(number);
            } else throw std::invalid_argument("Unknown option: "+key);
        }
    }
    validateRegionOptions(result.segmentation);
    if(result.nmsStepMm<=0||result.pruneLengthMm<0) throw std::invalid_argument("Invalid NMS or pruning distance");
    if(result.growthBandMm<0||result.seedCoreRatio<0||result.seedCoreRatio>1)
        throw std::invalid_argument("Invalid core growth band or seed ratio");
    if(result.growthBandMm>0&&result.seeds.empty()&&!result.nmsSeeds)
        throw std::invalid_argument("Core growth requires explicit seeds or --nms-seeds");
    for(double p:{result.frangi.alpha,result.frangi.beta,result.frangi.c})
        if(!std::isfinite(p)||p<=0) throw std::invalid_argument("Frangi parameters must be positive");
    return result;
}
inline std::string jsonString(const std::string& value) {
    std::string result="\"";const char* hex="0123456789abcdef";
    for(unsigned char c:value) {
        if(c=='"'||c=='\\') {result+='\\';result+=static_cast<char>(c);}
        else if(c<32) {result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=static_cast<char>(c);
    }
    return result+'"';
}
}

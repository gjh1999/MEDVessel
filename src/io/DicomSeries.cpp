#include <dcmtk/config/osconfig.h>
#include "io/DicomSeries.hpp"
#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmdata/dcxfer.h>
#include <algorithm>
#include <memory>
#include <string>
#include <cstdint>

namespace medvessel {
namespace {
struct Slice {
    std::unique_ptr<DcmFileFormat> file;
    Point3D position{};
    std::array<double,6> orientation{};
    double rowSpacing=0,columnSpacing=0,projection=0,slope=1,intercept=0;
    Uint16 rows=0,columns=0,bits=0,representation=0;
    std::string series;
};
double number(DcmDataset& data,const DcmTagKey& key,unsigned long index=0) {
    Float64 value=0;
    if(data.findAndGetFloat64(key,value,index).bad()||!std::isfinite(value))
        throw std::invalid_argument("Missing or invalid DICOM numeric metadata");
    return value;
}
Uint16 integer(DcmDataset& data,const DcmTagKey& key) {
    Uint16 value=0;if(data.findAndGetUint16(key,value).bad())
        throw std::invalid_argument("Missing DICOM integer metadata");
    return value;
}
std::string text(DcmDataset& data,const DcmTagKey& key) {
    OFString value;if(data.findAndGetOFString(key,value).bad())
        throw std::invalid_argument("Missing DICOM text metadata");
    return value.c_str();
}
}
Volume3D<float> readDicomSeries(const std::filesystem::path& directory) {
    if(!std::filesystem::is_directory(directory)) throw std::invalid_argument("DICOM input must be a directory");
    std::vector<Slice> slices;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        if(!entry.is_regular_file()) continue;
        auto file=std::make_unique<DcmFileFormat>();
        if(file->loadFile(entry.path().string().c_str()).bad()) {
            if(entry.path().extension()==".dcm") throw std::invalid_argument("Cannot read DICOM file");
            continue;
        }
        auto& data=*file->getDataset();if(!data.tagExists(DCM_PixelData)) continue;
        if(text(data,DCM_Modality)!="CT"||text(data,DCM_SOPClassUID)!=UID_CTImageStorage)
            throw std::invalid_argument("Only classic CT Image Storage supported");
        if(DcmXfer(data.getOriginalXfer()).usesEncapsulatedFormat())
            throw std::invalid_argument("Compressed DICOM pixels are not enabled");
        if(data.tagExists(DCM_NumberOfFrames)&&text(data,DCM_NumberOfFrames)!="1")
            throw std::invalid_argument("Multiframe DICOM is not supported");
        if(integer(data,DCM_SamplesPerPixel)!=1||text(data,DCM_PhotometricInterpretation)!="MONOCHROME2"||
           integer(data,DCM_BitsAllocated)!=16)
            throw std::invalid_argument("Only 16-bit MONOCHROME2 CT pixels supported");
        Slice slice;slice.rows=integer(data,DCM_Rows);slice.columns=integer(data,DCM_Columns);
        slice.bits=integer(data,DCM_BitsStored);slice.representation=integer(data,DCM_PixelRepresentation);
        if(!slice.rows||!slice.columns||!slice.bits||slice.bits>16||slice.representation>1||
           integer(data,DCM_HighBit)!=slice.bits-1) throw std::invalid_argument("Unsupported CT pixel encoding");
        for(unsigned long i=0;i<3;++i) slice.position[i]=number(data,DCM_ImagePositionPatient,i);
        for(unsigned long i=0;i<6;++i) slice.orientation[i]=number(data,DCM_ImageOrientationPatient,i);
        slice.rowSpacing=number(data,DCM_PixelSpacing,0);slice.columnSpacing=number(data,DCM_PixelSpacing,1);
        slice.series=text(data,DCM_SeriesInstanceUID);
        if(slice.series.empty()) throw std::invalid_argument("Empty SeriesInstanceUID");
        if(data.tagExists(DCM_RescaleSlope)) slice.slope=number(data,DCM_RescaleSlope);
        if(data.tagExists(DCM_RescaleIntercept)) slice.intercept=number(data,DCM_RescaleIntercept);
        if(slice.slope==0) throw std::invalid_argument("DICOM rescale slope is zero");
        slice.file=std::move(file);slices.push_back(std::move(slice));
    }
    if(slices.empty()) throw std::invalid_argument("No classic CT slices found");
    const auto orientation=slices.front().orientation;
    Point3D normal{orientation[1]*orientation[5]-orientation[2]*orientation[4],
        orientation[2]*orientation[3]-orientation[0]*orientation[5],
        orientation[0]*orientation[4]-orientation[1]*orientation[3]};
    for(auto& slice:slices) {
        if(slice.series!=slices.front().series||slice.rows!=slices.front().rows||slice.columns!=slices.front().columns)
            throw std::invalid_argument("Mixed DICOM series or dimensions");
        for(int a=0;a<6;++a) if(std::abs(slice.orientation[a]-orientation[a])>1e-6)
            throw std::invalid_argument("DICOM slice orientations differ");
        if(std::abs(slice.rowSpacing-slices.front().rowSpacing)>1e-6||
           std::abs(slice.columnSpacing-slices.front().columnSpacing)>1e-6)
            throw std::invalid_argument("DICOM pixel spacing differs");
        for(int a=0;a<3;++a) slice.projection+=slice.position[a]*normal[a];
    }
    std::sort(slices.begin(),slices.end(),[](const Slice& a,const Slice& b){return a.projection<b.projection;});
    double sliceSpacing=0;
    if(slices.size()>1) sliceSpacing=slices[1].projection-slices[0].projection;
    else {
        auto& data=*slices[0].file->getDataset();
        sliceSpacing=data.tagExists(DCM_SpacingBetweenSlices)?std::abs(number(data,DCM_SpacingBetweenSlices)):number(data,DCM_SliceThickness);
    }
    if(!std::isfinite(sliceSpacing)||sliceSpacing<=0) throw std::invalid_argument("Duplicate slice positions or invalid spacing");
    ImageGeometry geometry;geometry.spacing={slices[0].columnSpacing,slices[0].rowSpacing,sliceSpacing};
    for(int a=0;a<3;++a) {
        double sign=a<2?-1:1;geometry.origin[a]=sign*slices[0].position[a];
        geometry.direction[a*3]=sign*orientation[a];geometry.direction[a*3+1]=sign*orientation[a+3];
        geometry.direction[a*3+2]=sign*normal[a];
    }
    geometry.validate();
    for(std::size_t z=1;z<slices.size();++z) for(int a=0;a<3;++a) {
        double expected=slices[0].position[a]+normal[a]*sliceSpacing*z;
        if(std::abs(slices[z].position[a]-expected)>1e-3)
            throw std::invalid_argument("Nonuniform spacing or gantry tilt requires explicit resampling");
    }
    Volume3D<float> volume(slices[0].columns,slices[0].rows,slices.size(),geometry);
    for(std::size_t z=0;z<slices.size();++z) {
        auto& slice=slices[z];const Uint16* pixels=nullptr;unsigned long count=0;
        if(slice.file->getDataset()->findAndGetUint16Array(DCM_PixelData,pixels,&count).bad()||
           !pixels||count!=static_cast<unsigned long>(slice.rows)*slice.columns)
            throw std::invalid_argument("DICOM pixel data size mismatch");
        const auto bitMask=(1u<<slice.bits)-1;
        for(std::size_t y=0;y<slice.rows;++y) for(std::size_t x=0;x<slice.columns;++x) {
            std::int32_t stored=pixels[y*slice.columns+x]&bitMask;
            if(slice.representation&&(stored&(1u<<(slice.bits-1)))) stored-=static_cast<std::int32_t>(1u<<slice.bits);
            double hu=stored*slice.slope+slice.intercept;
            if(!std::isfinite(hu)||std::abs(hu)>std::numeric_limits<float>::max()) throw std::invalid_argument("Invalid rescaled CT value");
            volume.at(x,y,z)=static_cast<float>(hu);
        }
    }
    return volume;
}
}

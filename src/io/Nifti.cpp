#include "io/Nifti.hpp"
#include <nifti1_io.h>
#include <memory>
#include <cstdint>
#include <climits>
#include <filesystem>

namespace medvessel {
namespace {
using Image=std::unique_ptr<nifti_image,decltype(&nifti_image_free)>;
template<class T> double voxel(const nifti_image& image,std::size_t index) {
    return static_cast<double>(static_cast<const T*>(image.data)[index]);
}
double rawValue(const nifti_image& image,std::size_t i) {
    switch(image.datatype) {
        case NIFTI_TYPE_UINT8:return voxel<std::uint8_t>(image,i);
        case NIFTI_TYPE_INT8:return voxel<std::int8_t>(image,i);
        case NIFTI_TYPE_INT16:return voxel<std::int16_t>(image,i);
        case NIFTI_TYPE_UINT16:return voxel<std::uint16_t>(image,i);
        case NIFTI_TYPE_INT32:return voxel<std::int32_t>(image,i);
        case NIFTI_TYPE_UINT32:return voxel<std::uint32_t>(image,i);
        case NIFTI_TYPE_FLOAT32:return voxel<float>(image,i);
        case NIFTI_TYPE_FLOAT64:return voxel<double>(image,i);
        default:throw std::invalid_argument("Unsupported NIfTI scalar type");
    }
}
}
Volume3D<float> readNifti(const std::string& path) {
    Image image(nifti_image_read(path.c_str(),1),nifti_image_free);
    if(!image||!image->data) throw std::runtime_error("Cannot read NIfTI: "+path);
    if(image->nx<=0||image->ny<=0||image->nz<=0)
        throw std::invalid_argument("Invalid NIfTI spatial dimensions");
    for(int axis=4;axis<=7;++axis) if(image->dim[axis]>1)
        throw std::invalid_argument("Only scalar spatial volumes supported");
    double units=0;
    switch(image->xyz_units) {
        case NIFTI_UNITS_MM:units=1;break;
        case NIFTI_UNITS_METER:units=1000;break;
        case NIFTI_UNITS_MICRON:units=0.001;break;
        default:throw std::invalid_argument("NIfTI physical units are unspecified");
    }
    if(image->sform_code<=0&&image->qform_code<=0)
        throw std::invalid_argument("NIfTI requires an explicit sform or qform");
    auto affine=image->sform_code>0?image->sto_xyz:image->qto_xyz;
    ImageGeometry geometry;
    for(int row=0;row<3;++row) geometry.origin[row]=affine.m[row][3]*units;
    for(int col=0;col<3;++col) {
        double length=0;
        for(int row=0;row<3;++row) length=std::hypot(length,affine.m[row][col]*units);
        if(!std::isfinite(length)||length<=0) throw std::invalid_argument("Invalid NIfTI affine");
        geometry.spacing[col]=length;
        for(int row=0;row<3;++row) geometry.direction[row*3+col]=affine.m[row][col]*units/length;
    }
    geometry.validate();
    Volume3D<float> volume(image->nx,image->ny,image->nz,geometry);
    if(volume.data().size()!=image->nvox) throw std::invalid_argument("NIfTI voxel count mismatch");
    double slope=image->scl_slope,intercept=image->scl_inter;
    if(slope==0) {slope=1;intercept=0;}
    if(!std::isfinite(slope)||!std::isfinite(intercept)) throw std::invalid_argument("Invalid NIfTI scaling");
    auto d=volume.dimensions();std::size_t i=0;
    for(std::size_t z=0;z<d[2];++z) for(std::size_t y=0;y<d[1];++y) for(std::size_t x=0;x<d[0];++x,++i) {
        double value=rawValue(*image,i)*slope+intercept;
        if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())
            throw std::invalid_argument("Nonfinite or overflowing NIfTI intensity");
        volume.at(x,y,z)=static_cast<float>(value);
    }
    return volume;
}
void writeNifti(const Volume3D<float>& volume,const std::string& path) {
    const bool nii=path.size()>=4&&path.substr(path.size()-4)==".nii";
    const bool gzip=path.size()>=7&&path.substr(path.size()-7)==".nii.gz";
    if(!nii&&!gzip) throw std::invalid_argument("Output must end in .nii or .nii.gz");
    auto d=volume.dimensions();
    for(auto size:d) if(size>32767) throw std::invalid_argument("NIfTI-1 dimension exceeds int16 range");
    int dimensions[8]={3,static_cast<int>(d[0]),static_cast<int>(d[1]),static_cast<int>(d[2]),1,1,1,1};
    Image image(nifti_make_new_nim(dimensions,NIFTI_TYPE_FLOAT32,1),nifti_image_free);
    if(!image||!image->data) throw std::bad_alloc();
    auto& g=volume.geometry();
    for(double value:g.origin) if(!std::isfinite(static_cast<float>(value)))
        throw std::invalid_argument("Origin exceeds NIfTI-1 numeric range");
    for(double value:g.spacing) if(!std::isfinite(static_cast<float>(value))||static_cast<float>(value)<=0)
        throw std::invalid_argument("Spacing exceeds NIfTI-1 numeric range");
    image->xyz_units=NIFTI_UNITS_MM;image->sform_code=NIFTI_XFORM_SCANNER_ANAT;
    image->qform_code=0;image->scl_slope=1;image->scl_inter=0;
    image->sto_xyz={};image->sto_xyz.m[3][3]=1;
    for(int row=0;row<3;++row) {
        image->sto_xyz.m[row][3]=static_cast<float>(g.origin[row]);
        for(int col=0;col<3;++col)
            image->sto_xyz.m[row][col]=static_cast<float>(g.direction[row*3+col]*g.spacing[col]);
    }
    image->sto_ijk=nifti_mat44_inverse(image->sto_xyz);
    image->dx=image->pixdim[1]=static_cast<float>(g.spacing[0]);
    image->dy=image->pixdim[2]=static_cast<float>(g.spacing[1]);
    image->dz=image->pixdim[3]=static_cast<float>(g.spacing[2]);
    for(std::size_t i=0;i<volume.data().size();++i) {
        if(!std::isfinite(volume.data()[i])) throw std::invalid_argument("Cannot export nonfinite intensity");
        static_cast<float*>(image->data)[i]=volume.data()[i];
    }
    if(nifti_set_filenames(image.get(),path.c_str(),0,1)!=0)
        throw std::runtime_error("Cannot set NIfTI output filename");
    if(nifti_image_write_status(image.get())!=0)
        throw std::runtime_error("Cannot write NIfTI: "+path);
}
} // namespace medvessel

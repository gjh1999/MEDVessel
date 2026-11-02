#include "io/Nifti.hpp"
#include <nifti1_io.h>
#include <iostream>
#include <filesystem>
#include <memory>
void require(bool v) {if(!v) throw std::runtime_error("NIfTI check failed");}
#include "Test.hpp"
MEDVESSEL_TEST(Nifti) {
    using namespace medvessel;
    ImageGeometry g;g.spacing={0.5,0.75,2};g.origin={10,-20,30};
    g.direction={0,-1,0,1,0,0,0,0,-1};
    Volume3D<float> volume(4,5,6,g);
    for(std::size_t z=0;z<6;++z) for(std::size_t y=0;y<5;++y) for(std::size_t x=0;x<4;++x)
        volume.at(x,y,z)=static_cast<float>(x+10*y+100*z)-1000;
    for(auto filename:{"roundtrip.nii","roundtrip.nii.gz"}) {
        writeNifti(volume,filename);auto read=readNifti(filename);
        require(read.data()==volume.data());require(read.geometry().spacing==g.spacing);
        require(read.geometry().origin==g.origin);require(read.geometry().direction==g.direction);
        std::filesystem::remove(filename);
    }
    int dims[8]={3,2,2,2,1,1,1,1};
    std::unique_ptr<nifti_image,decltype(&nifti_image_free)> image(
        nifti_make_new_nim(dims,NIFTI_TYPE_INT16,1),nifti_image_free);
    image->xyz_units=NIFTI_UNITS_METER;image->sform_code=1;image->sto_xyz={};
    for(int a=0;a<3;++a) image->sto_xyz.m[a][a]=0.002f;
    image->sto_xyz.m[3][3]=1;image->scl_slope=2;image->scl_inter=-1000;
    static_cast<short*>(image->data)[0]=42;
    nifti_set_filenames(image.get(),"scaled.nii",0,1);nifti_image_write(image.get());
    auto scaled=readNifti("scaled.nii");require(scaled.at(0,0,0)==-916);
    require(std::abs(scaled.geometry().spacing[0]-2)<1e-6);
    image->sto_xyz.m[0][1]=0.001f;
    nifti_image_write(image.get());
    bool rejected=false;
    try {readNifti("scaled.nii");} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected);std::filesystem::remove("scaled.nii");
    image->sform_code=0;image->qform_code=1;
    image->xyz_units=NIFTI_UNITS_MM;
    image->quatern_b=image->quatern_c=image->quatern_d=0;
    image->qoffset_x=10;image->qoffset_y=20;image->qoffset_z=30;
    image->dx=image->pixdim[1]=0.5;image->dy=image->pixdim[2]=1;image->dz=image->pixdim[3]=2;image->qfac=-1;
    nifti_set_filenames(image.get(),"qform.nii",0,1);nifti_image_write(image.get());
    auto qform=readNifti("qform.nii");
    require(qform.geometry().origin==Point3D{10,20,30});
    require(qform.geometry().spacing==Point3D{0.5,1,2});
    require(qform.geometry().direction[8]==-1);
    std::filesystem::remove("qform.nii");
    std::cout<<"NIfTI tests passed\n";
}

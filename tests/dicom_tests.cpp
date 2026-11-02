#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmdata/dctk.h>
#include "io/DicomSeries.hpp"
#include "Test.hpp"
#include <iostream>

void require(bool v) {if(!v) throw std::runtime_error("DICOM check failed");}
void writeSlice(const std::filesystem::path& path,int slice,double z,
                const char* series="1.2.826.0.1.3680043.10.543.1",bool oblique=false) {
    DcmFileFormat file;auto& data=*file.getDataset();
    data.putAndInsertString(DCM_SOPClassUID,UID_CTImageStorage);
    std::string instance=std::string(series)+'.'+std::to_string(slice+1);
    data.putAndInsertString(DCM_SOPInstanceUID,instance.c_str());
    data.putAndInsertString(DCM_SeriesInstanceUID,series);
    data.putAndInsertString(DCM_Modality,"CT");
    data.putAndInsertString(DCM_PhotometricInterpretation,"MONOCHROME2");
    data.putAndInsertUint16(DCM_Rows,2);data.putAndInsertUint16(DCM_Columns,3);
    data.putAndInsertUint16(DCM_SamplesPerPixel,1);data.putAndInsertUint16(DCM_BitsAllocated,16);
    data.putAndInsertUint16(DCM_BitsStored,12);data.putAndInsertUint16(DCM_HighBit,11);
    data.putAndInsertUint16(DCM_PixelRepresentation,1);
    data.putAndInsertString(DCM_ImageOrientationPatient,oblique?"0\\1\\0\\0\\0\\1":"1\\0\\0\\0\\1\\0");
    std::string position=oblique?std::to_string(z)+"\\20\\-5":"10\\20\\"+std::to_string(z);
    data.putAndInsertString(DCM_ImagePositionPatient,position.c_str());
    data.putAndInsertString(DCM_PixelSpacing,"0.5\\0.75");
    data.putAndInsertString(DCM_RescaleSlope,"2");data.putAndInsertString(DCM_RescaleIntercept,"-1000");
    Uint16 pixels[6]={static_cast<Uint16>(slice*10),1,2,3,4,4095};
    data.putAndInsertUint16Array(DCM_PixelData,pixels,6);
    require(file.saveFile(path.string().c_str(),EXS_LittleEndianExplicit).good());
}
MEDVESSEL_TEST(Dicom) {
    using namespace medvessel;std::filesystem::path directory="dicom_fixture";
    std::filesystem::create_directories(directory);
    writeSlice(directory/"c.dcm",0,-5);writeSlice(directory/"a.dcm",1,-3);writeSlice(directory/"b.dcm",2,-1);
    auto volume=readDicomSeries(directory);
    require(volume.dimensions()==std::array<std::size_t,3>{3,2,3});
    require(volume.geometry().origin==Point3D{-10,-20,-5});
    require(volume.geometry().spacing==Point3D{0.75,0.5,2});
    require(volume.geometry().direction==std::array<double,9>{-1,0,0,0,-1,0,0,0,1});
    require(volume.at(0,0,0)==-1000&&volume.at(0,0,2)==-960&&volume.at(2,1,0)==-1002);
    writeSlice(directory/"b.dcm",2,0);
    bool rejected=false;try {readDicomSeries(directory);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected);
    writeSlice(directory/"b.dcm",2,-1,"1.2.826.0.1.3680043.10.543.2");
    rejected=false;try {readDicomSeries(directory);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected);
    writeSlice(directory/"b.dcm",2,-1);
    std::filesystem::path rotatedDirectory="dicom_rotated_fixture";
    std::filesystem::create_directories(rotatedDirectory);
    const char* uid="1.2.826.0.1.3680043.10.543.3";
    writeSlice(rotatedDirectory/"c.dcm",0,10,uid,true);
    writeSlice(rotatedDirectory/"a.dcm",1,12,uid,true);
    writeSlice(rotatedDirectory/"b.dcm",2,14,uid,true);
    auto rotated=readDicomSeries(rotatedDirectory);
    require(rotated.geometry().direction==std::array<double,9>{0,0,-1,-1,0,0,0,1,0});
    require(rotated.at(0,0,2)==-960);
    std::cout<<"DICOM tests passed\n";
}

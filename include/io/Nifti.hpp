#pragma once
#include "core/Volume3D.hpp"
#include <string>

namespace medvessel {
// NIfTI RAS world coordinates, converted to mm. sform takes precedence over
// qform. Unknown physical units, absent transforms and sheared grids are rejected.
Volume3D<float> readNifti(const std::string& path);
void writeNifti(const Volume3D<float>& volume,const std::string& path);
}

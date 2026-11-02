#pragma once
#include "core/Volume3D.hpp"
#include <filesystem>

namespace medvessel {
// Classic, single-frame, uncompressed 16-bit CT only. One SeriesInstanceUID per
// directory; slices sorted by physical position. DICOM LPS is converted to RAS mm.
Volume3D<float> readDicomSeries(const std::filesystem::path& directory);
}

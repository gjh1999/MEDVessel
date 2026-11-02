#pragma once
#include "core/Volume3D.hpp"
#include <algorithm>

namespace medvessel {
// Sigma is measured in mm. Separable convolution, normalized truncated kernels,
// and replicated edge voxels define the boundary behaviour.
inline Volume3D<float> gaussianFilter3D(const Volume3D<float>& input,
                                       double sigmaMm, double truncate = 3.0) {
    if (!std::isfinite(sigmaMm) || sigmaMm <= 0 ||
        !std::isfinite(truncate) || truncate <= 0)
        throw std::invalid_argument("Gaussian parameters must be finite and positive");
    auto current = input;
    const auto dims = input.dimensions();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double sigma = sigmaMm / input.geometry().spacing[axis];
        double radiusValue = std::ceil(truncate * sigma);
        if (!std::isfinite(radiusValue) || radiusValue > 100000)
            throw std::invalid_argument("Gaussian kernel exceeds supported radius");
        auto radius = static_cast<long long>(radiusValue);
        std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
        double sum = 0;
        for (long long k = -radius; k <= radius; ++k) {
            double ratio = static_cast<double>(k) / sigma;
            double value = k == 0 ? 1.0 : std::exp(-0.5 * ratio * ratio);
            kernel[static_cast<std::size_t>(k + radius)] = value;
            sum += value;
        }
        for (auto& value : kernel) value /= sum;
        Volume3D<float> output(dims[0], dims[1], dims[2], input.geometry());
        #ifdef _OPENMP
        #pragma omp parallel for schedule(static)
        #endif
        for (std::ptrdiff_t zz = 0; zz < static_cast<std::ptrdiff_t>(dims[2]); ++zz) {
            const auto z = static_cast<std::size_t>(zz);
            for (std::size_t y = 0; y < dims[1]; ++y)
                for (std::size_t x = 0; x < dims[0]; ++x) {
                    std::array<std::size_t, 3> p{x,y,z};
                    double value = 0;
                    for (long long k = -radius; k <= radius; ++k) {
                        auto q = p;
                        // Clamp without converting potentially large dimensions to signed integers.
                        if (k < 0) q[axis] = p[axis] < static_cast<std::size_t>(-k)
                            ? 0 : p[axis] - static_cast<std::size_t>(-k);
                        else q[axis] = p[axis] + std::min(static_cast<std::size_t>(k), dims[axis]-1-p[axis]);
                        value += kernel[static_cast<std::size_t>(k+radius)] * current.at(q[0],q[1],q[2]);
                    }
                    output.at(x,y,z) = static_cast<float>(value);
                }
        }
        current = std::move(output);
    }
    return current;
}
} // namespace medvessel

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace medvessel {

using Point3D = std::array<double, 3>;

// World coordinates use millimetres; direction is a row-major matrix.
struct ImageGeometry {
    Point3D spacing{1, 1, 1};
    Point3D origin{0, 0, 0};
    std::array<double, 9> direction{1, 0, 0, 0, 1, 0, 0, 0, 1};

    void validate() const {
        for (double s : spacing)
            if (!std::isfinite(s) || s <= 0)
                throw std::invalid_argument("Spacing must be finite and positive");
        for (double o : origin)
            if (!std::isfinite(o)) throw std::invalid_argument("Invalid origin");
        for (double d : direction)
            if (!std::isfinite(d)) throw std::invalid_argument("Invalid direction");
        // Accept rotations and reflections, but reject scale/shear in direction.
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t j = 0; j < 3; ++j) {
                double dot = 0;
                for (std::size_t k = 0; k < 3; ++k)
                    dot += direction[k * 3 + i] * direction[k * 3 + j];
                if (std::abs(dot - (i == j ? 1.0 : 0.0)) > 1e-6)
                    throw std::invalid_argument("Direction must be orthonormal");
            }
    }

    Point3D indexToWorld(const Point3D& index) const {
        validate();
        Point3D world = origin;
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t j = 0; j < 3; ++j)
                world[i] += direction[i * 3 + j] * index[j] * spacing[j];
        return world;
    }

    Point3D worldToIndex(const Point3D& world) const {
        validate();
        Point3D index{};
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j)
                index[i] += direction[j * 3 + i] * (world[j] - origin[j]);
            index[i] /= spacing[i];
        }
        return index;
    }
};

template<class T>
class Volume3D {
public:
    Volume3D(std::size_t width, std::size_t height, std::size_t depth,
             ImageGeometry geometry = {}, T initial = T{})
        : dimensions_{width, height, depth}, geometry_(geometry) {
        geometry_.validate();
        std::size_t count = 1;
        for (auto dimension : dimensions_) {
            if (dimension == 0) throw std::invalid_argument("Empty volume dimension");
            if (count > std::numeric_limits<std::size_t>::max() / dimension)
                throw std::overflow_error("Volume dimensions overflow");
            count *= dimension;
        }
        data_.assign(count, initial);
    }

    const std::array<std::size_t, 3>& dimensions() const { return dimensions_; }
    const ImageGeometry& geometry() const { return geometry_; }
    const std::vector<T>& data() const { return data_; }
    T& at(std::size_t x, std::size_t y, std::size_t z) { return data_[offset(x,y,z)]; }
    const T& at(std::size_t x, std::size_t y, std::size_t z) const { return data_[offset(x,y,z)]; }

private:
    std::size_t offset(std::size_t x, std::size_t y, std::size_t z) const {
        if (x >= dimensions_[0] || y >= dimensions_[1] || z >= dimensions_[2])
            throw std::out_of_range("Voxel index outside volume");
        return (z * dimensions_[1] + y) * dimensions_[0] + x;
    }
    std::array<std::size_t, 3> dimensions_;
    ImageGeometry geometry_;
    std::vector<T> data_;
};
} // namespace medvessel

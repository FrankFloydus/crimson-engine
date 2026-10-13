#include "Mesh.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace crimson {

MeshData makeUvSphere(std::uint32_t segments, std::uint32_t rings, float radius) {
    if (segments < 3 || rings < 2 || !std::isfinite(radius) || radius <= 0.0f) {
        throw std::invalid_argument("Sphere needs at least 3 segments, 2 rings, and a finite positive radius.");
    }
    const std::uint64_t columns = std::uint64_t{segments} + 1;
    const std::uint64_t rows = std::uint64_t{rings} + 1;
    constexpr auto indexLimit = std::numeric_limits<std::uint16_t>::max();
    // Check before multiplying: two UINT32_MAX inputs would overflow even uint64_t.
    if (columns > indexLimit || rows > indexLimit || columns > indexLimit / rows) {
        throw std::length_error("Sphere exceeds the 16-bit vertex index limit.");
    }
    const std::uint64_t vertexCount = columns * rows;
    MeshData data;
    data.vertices.reserve(static_cast<std::size_t>(vertexCount));
    data.indices.reserve(static_cast<std::size_t>(6ull * segments * (rings - 1)));
    for (std::uint32_t row = 0; row <= rings; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(rings);
        const float theta = v * std::numbers::pi_v<float>;
        const float sinTheta = row == 0 || row == rings ? 0.0f : std::sin(theta);
        const float cosTheta = row == 0 ? 1.0f : row == rings ? -1.0f : std::cos(theta);
        for (std::uint32_t col = 0; col <= segments; ++col) {
            const float u = static_cast<float>(col) / static_cast<float>(segments);
            const float phi = col == segments ? 0.0f : u * 2.0f * std::numbers::pi_v<float>;
            const float nx = std::cos(phi) * sinTheta;
            const float ny = cosTheta;
            const float nz = std::sin(phi) * sinTheta;
            // Analytic +U tangent remains defined at both poles, where dP/du is
            // zero. Duplicating it at U=0/1 also makes the seam agree exactly.
            const float tx = -std::sin(phi);
            const float tz = std::cos(phi);
            data.vertices.push_back({nx * radius, ny * radius, nz * radius, nx, ny, nz, u, v,
                                     tx, 0.0f, tz, 1.0f});
        }
    }
    for (std::uint32_t row = 0; row < rings; ++row) {
        for (std::uint32_t col = 0; col < segments; ++col) {
            const auto a = static_cast<std::uint16_t>(row * (segments + 1) + col);
            const auto b = static_cast<std::uint16_t>((row + 1) * (segments + 1) + col);
            if (row > 0) {
                data.indices.insert(data.indices.end(), {a, static_cast<std::uint16_t>(a + 1), b});
            }
            if (row < rings - 1) {
                data.indices.insert(data.indices.end(), {static_cast<std::uint16_t>(a + 1), static_cast<std::uint16_t>(b + 1), b});
            }
        }
    }
    return data;
}

} // namespace crimson

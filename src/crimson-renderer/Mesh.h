#pragma once

#include <cstdint>
#include <vector>

namespace crimson {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    // Tangent points toward increasing U; B = cross(N, T) * handedness.
    // Defaults keep the shared fullscreen/grid vertex format well-defined.
    float tx{1.0f}, ty{0.0f}, tz{0.0f}, tw{1.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

// CPU-only generation. Throws for invalid inputs or meshes beyond 16-bit indexing.
MeshData makeUvSphere(std::uint32_t segments = 64, std::uint32_t rings = 32, float radius = 1.0f);

} // namespace crimson

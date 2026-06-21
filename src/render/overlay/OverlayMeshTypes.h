#pragma once

#include <cstdint>
#include <vector>

namespace quader {

struct OverlayFloat2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct OverlayFloat3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct OverlayFloat4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct OverlayFaceFillVertex {
    OverlayFloat3 position;
    OverlayFloat2 uv;
    std::uint32_t color = 0;
};

struct OverlayLineVertex {
    OverlayFloat4 position;
    OverlayFloat4 lineDistancePixels;
    std::uint32_t color = 0;
};

struct OverlayPointVertex {
    OverlayFloat4 position;
    OverlayFloat2 uv;
    std::uint32_t color = 0;
};

template <typename Vertex>
struct OverlayMesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    bool empty() const {
        return vertices.empty() || indices.empty();
    }
};

using OverlayFaceFillMesh = OverlayMesh<OverlayFaceFillVertex>;
using OverlayLineMesh = OverlayMesh<OverlayLineVertex>;
using OverlayPointMesh = OverlayMesh<OverlayPointVertex>;

} // namespace quader

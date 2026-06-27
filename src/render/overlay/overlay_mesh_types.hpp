////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <vector>

namespace quader {

/**
 * Represents an Overlay Float2 value used by the render extraction and Filament resource layer.
 */
struct OverlayFloat2 {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * Represents an Overlay Float3 value used by the render extraction and Filament resource layer.
 */
struct OverlayFloat3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/**
 * Represents an Overlay Float4 value used by the render extraction and Filament resource layer.
 */
struct OverlayFloat4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

/**
 * Represents an Overlay Face Fill Vertex value used by the render extraction and Filament resource layer.
 */
struct OverlayFaceFillVertex {
    OverlayFloat3 position;
    OverlayFloat2 uv;
    std::uint32_t color = 0;
};

/**
 * Represents an Overlay Line Vertex value used by the render extraction and Filament resource layer.
 */
struct OverlayLineVertex {
    OverlayFloat4 position;
    OverlayFloat4 lineDistancePixels;
    std::uint32_t color = 0;
};

/**
 * Represents an Overlay Point Vertex value used by the render extraction and Filament resource layer.
 */
struct OverlayPointVertex {
    OverlayFloat4 position;
    OverlayFloat2 uv;
    std::uint32_t color = 0;
};

template <typename Vertex>
/**
 * Represents an Overlay Mesh value used by the render extraction and Filament resource layer.
 */
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

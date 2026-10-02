////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
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
    // The line remains in world space so camera navigation can update only
    // Filament's view uniforms. The vertex shader clips and expands these
    // endpoints into a constant-pixel ribbon using the per-corner control.
    OverlayFloat4 position;
    OverlayFloat4 start;
    OverlayFloat4 end;
    OverlayFloat4 control;
    // One off-edge vertex per incident face lets the shader evaluate the
    // surface depth across the expanded ribbon without camera-dependent CPU work.
    OverlayFloat4 support0;
    OverlayFloat4 support1;
    std::uint32_t color = 0;
};

/**
 * Represents an Overlay Point Vertex value used by the render extraction and Filament resource layer.
 */
struct OverlayPointVertex {
    // Point centers remain in world space. The point material projects the
    // center and expands this canonical quad in device space, so camera motion
    // never requires rebuilding the dense point buffer.
    OverlayFloat4 position;
    OverlayFloat4 center;
    OverlayFloat2 uv;
    std::uint32_t color = 0;
};

/**
 * Represents an Overlay Icon Vertex value used by the render extraction and Filament resource layer.
 */
struct OverlayIconVertex {
    OverlayFloat4 position;
    OverlayFloat2 uv;
    std::uint32_t color = 0;
};

/// Selects one independently uploaded world-position attribute stream.
enum class OverlayPositionStream : std::uint8_t {
    Primary,
    Secondary,
    Support0,
    Support1,
};

/**
 * Maps one semantic mesh element to a retained range of expanded GPU vertices.
 * Point/line quads keep their indices, UVs, controls, colors and render state
 * immutable while transform previews update only these position streams.
 */
struct OverlayPositionStreamBinding {
    int object_id = -1;
    int vertex_id = -1;
    std::uint32_t first_vertex = 0u;
    std::uint32_t vertex_count = 0u;
    OverlayPositionStream stream = OverlayPositionStream::Primary;
    OverlayFloat4 base_position;
    std::uint32_t source_index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t position_index = std::numeric_limits<std::uint32_t>::max();
};

template <typename Vertex>
/**
 * Represents an Overlay Mesh value used by the render extraction and Filament resource layer.
 */
struct OverlayMesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<OverlayPositionStreamBinding> position_stream_bindings;

    bool empty() const {
        return vertices.empty() || indices.empty();
    }
};

using OverlayFaceFillMesh = OverlayMesh<OverlayFaceFillVertex>;
using OverlayLineMesh = OverlayMesh<OverlayLineVertex>;
using OverlayEditWireMesh = OverlayMesh<OverlayLineVertex>;
using OverlayPointMesh = OverlayMesh<OverlayPointVertex>;
using OverlayIconMesh = OverlayMesh<OverlayIconVertex>;

} // namespace quader

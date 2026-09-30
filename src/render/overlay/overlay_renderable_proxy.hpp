////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_mesh_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace filament {
class IndexBuffer;
class MaterialInstance;
class VertexBuffer;
} // namespace filament

namespace quader {

/**
 * Enumerates overlay proxy geometry families used for safe in-place buffer updates.
 */
enum class OverlayRenderableProxyKind {
    Unknown,
    Triangle,
    Line,
    EditWire,
    Point,
    Icon,
};

/// Selection presentation occupies its own stream over retained marker quads.
struct OverlayComponentPointAppearance {
    std::uint32_t color = 0u;
    OverlayFloat4 control;
};

/**
 * Represents an Overlay Renderable Proxy value used by the render extraction and Filament resource layer.
 */
struct OverlayRenderableProxy {
    OverlayRenderableProxy() = default;
    OverlayRenderableProxy(const OverlayRenderableProxy&) = delete;
    OverlayRenderableProxy& operator=(const OverlayRenderableProxy&) = delete;
    OverlayRenderableProxy(OverlayRenderableProxy &&other) noexcept
        : entity_id(std::exchange(other.entity_id, 0u)),
          vertex_buffer(std::exchange(other.vertex_buffer, nullptr)),
          index_buffer(std::exchange(other.index_buffer, nullptr)),
          material_instance(std::exchange(other.material_instance, nullptr)),
          occluded_material_instance(
              std::exchange(other.occluded_material_instance, nullptr)),
          component_point_materials(std::exchange(other.component_point_materials, {})),
          face_fill_materials(std::move(other.face_fill_materials)),
          face_fill_colors(std::move(other.face_fill_colors)),
          face_fill_triangle_colors(std::move(other.face_fill_triangle_colors)),
          kind(std::exchange(other.kind, OverlayRenderableProxyKind::Unknown)),
          vertex_count(std::exchange(other.vertex_count, 0u)),
          index_count(std::exchange(other.index_count, 0u)),
          edit_wire_slot_capacity(std::exchange(other.edit_wire_slot_capacity, 0u)),
          edit_wire_update_revision(std::exchange(other.edit_wire_update_revision, 0u)),
          shared_positions_bound(std::exchange(other.shared_positions_bound, false)),
          position_stream_bindings(
              std::move(other.position_stream_bindings)),
          position_stream3(std::move(other.position_stream3)),
          primary_position_stream4(
              std::move(other.primary_position_stream4)),
          secondary_position_stream4(
              std::move(other.secondary_position_stream4)),
          visibility_point_indices(std::move(other.visibility_point_indices)),
          point_visibility_stream(std::move(other.point_visibility_stream)),
          component_point_appearance(std::move(other.component_point_appearance)) {}
    OverlayRenderableProxy& operator=(OverlayRenderableProxy&&) noexcept = delete;

    std::uint32_t entity_id = 0;
    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    filament::MaterialInstance *material_instance = nullptr;
    // Optional second draw over the same buffers, used by object silhouettes.
    filament::MaterialInstance *occluded_material_instance = nullptr;
    // The first component pass uses material_instance; all four share one VB/IB.
    std::array<filament::MaterialInstance *, 3u> component_point_materials{};
    // Additional face composition passes share the primary pass's VB/IB.
    std::vector<filament::MaterialInstance *> face_fill_materials;
    std::vector<std::uint32_t> face_fill_colors;
    std::vector<std::uint32_t> face_fill_triangle_colors;
    OverlayRenderableProxyKind kind = OverlayRenderableProxyKind::Unknown;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    // Retained SourceWire buffers hold four vertices/six indices per slot;
    // index_count is the active prefix, independently of allocated capacity.
    std::uint32_t edit_wire_slot_capacity = 0u;
    std::uint64_t edit_wire_update_revision = 0u;
    bool shared_positions_bound = false;
    std::vector<OverlayPositionStreamBinding> position_stream_bindings;
    std::vector<OverlayFloat3> position_stream3;
    std::vector<OverlayFloat4> primary_position_stream4;
    std::vector<OverlayFloat4> secondary_position_stream4;
    // One frame-point index per canonical four-vertex marker quad.
    std::vector<std::size_t> visibility_point_indices;
    std::vector<float> point_visibility_stream;
    std::vector<OverlayComponentPointAppearance> component_point_appearance;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_mesh_types.hpp"

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
          kind(std::exchange(other.kind, OverlayRenderableProxyKind::Unknown)),
          vertex_count(std::exchange(other.vertex_count, 0u)),
          index_count(std::exchange(other.index_count, 0u)),
          position_stream_bindings(
              std::move(other.position_stream_bindings)),
          position_stream3(std::move(other.position_stream3)),
          primary_position_stream4(
              std::move(other.primary_position_stream4)),
          secondary_position_stream4(
              std::move(other.secondary_position_stream4)) {}
    OverlayRenderableProxy& operator=(OverlayRenderableProxy&&) noexcept = delete;

    std::uint32_t entity_id = 0;
    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    filament::MaterialInstance *material_instance = nullptr;
    // Optional second draw over the same buffers, used by object silhouettes.
    filament::MaterialInstance *occluded_material_instance = nullptr;
    OverlayRenderableProxyKind kind = OverlayRenderableProxyKind::Unknown;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::vector<OverlayPositionStreamBinding> position_stream_bindings;
    std::vector<OverlayFloat3> position_stream3;
    std::vector<OverlayFloat4> primary_position_stream4;
    std::vector<OverlayFloat4> secondary_position_stream4;
};

} // namespace quader

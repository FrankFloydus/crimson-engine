////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <utility>

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
    Point,
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
          kind(std::exchange(other.kind, OverlayRenderableProxyKind::Unknown)),
          vertex_count(std::exchange(other.vertex_count, 0u)),
          index_count(std::exchange(other.index_count, 0u)) {}
    OverlayRenderableProxy& operator=(OverlayRenderableProxy&&) noexcept = delete;

    std::uint32_t entity_id = 0;
    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    filament::MaterialInstance *material_instance = nullptr;
    OverlayRenderableProxyKind kind = OverlayRenderableProxyKind::Unknown;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
};

} // namespace quader

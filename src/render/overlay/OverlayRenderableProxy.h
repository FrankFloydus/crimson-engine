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
 * Represents an Overlay Renderable Proxy value used by the render extraction and Filament resource layer.
 */
struct OverlayRenderableProxy {
    OverlayRenderableProxy() = default;
    OverlayRenderableProxy(const OverlayRenderableProxy&) = delete;
    OverlayRenderableProxy& operator=(const OverlayRenderableProxy&) = delete;
    OverlayRenderableProxy(OverlayRenderableProxy&& other) noexcept
        : entityId(std::exchange(other.entityId, 0u))
        , vertexBuffer(std::exchange(other.vertexBuffer, nullptr))
        , indexBuffer(std::exchange(other.indexBuffer, nullptr))
        , materialInstance(std::exchange(other.materialInstance, nullptr)) {}
    OverlayRenderableProxy& operator=(OverlayRenderableProxy&&) noexcept = delete;

    std::uint32_t entityId = 0;
    filament::VertexBuffer* vertexBuffer = nullptr;
    filament::IndexBuffer* indexBuffer = nullptr;
    filament::MaterialInstance* materialInstance = nullptr;
};

} // namespace quader

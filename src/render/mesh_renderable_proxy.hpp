////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

#include <filament/IndexBuffer.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace quader {

/**
 * Represents a Mesh Renderable Proxy value used by the render extraction and Filament resource layer.
 */
struct MeshRenderableProxy {
    MeshRenderableProxy() = default;
    MeshRenderableProxy(const MeshRenderableProxy&) = delete;
    MeshRenderableProxy& operator=(const MeshRenderableProxy&) = delete;
    MeshRenderableProxy(MeshRenderableProxy&& other) noexcept {
        moveFrom(std::move(other));
    }
    MeshRenderableProxy& operator=(MeshRenderableProxy&&) noexcept = delete;

    utils::Entity entity;
    EntityId sourceEntity = InvalidEntityId;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
    std::uint64_t meshRevision = 0;
    std::uint64_t materialRevision = 0;
    bool castShadows = false;
    bool doubleSided = false;
    filament::VertexBuffer* vertexBuffer = nullptr;
    filament::IndexBuffer* indexBuffer = nullptr;
    std::vector<std::uint32_t> materialSlots;
    std::vector<filament::MaterialInstance*> materialInstances;

private:
    void moveFrom(MeshRenderableProxy&& other) noexcept {
        entity = other.entity;
        other.entity.clear();
        sourceEntity = std::exchange(other.sourceEntity, InvalidEntityId);
        mesh = std::exchange(other.mesh, InvalidAssetId);
        material = std::exchange(other.material, InvalidAssetId);
        meshRevision = std::exchange(other.meshRevision, 0);
        materialRevision = std::exchange(other.materialRevision, 0);
        castShadows = std::exchange(other.castShadows, false);
        doubleSided = std::exchange(other.doubleSided, false);
        vertexBuffer = std::exchange(other.vertexBuffer, nullptr);
        indexBuffer = std::exchange(other.indexBuffer, nullptr);
        materialSlots = std::move(other.materialSlots);
        materialInstances = std::move(other.materialInstances);
    }
};

} // namespace quader

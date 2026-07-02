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
      move_from(std::move(other));
    }
    MeshRenderableProxy& operator=(MeshRenderableProxy&&) noexcept = delete;

    utils::Entity entity;
    EntityId source_entity = kInvalidEntityId;
    AssetId mesh = kInvalidAssetId;
    AssetId material = kInvalidAssetId;
    std::uint64_t mesh_revision = 0;
    std::uint64_t material_revision = 0;
    bool cast_shadows = false;
    bool double_sided = false;
    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    std::vector<std::uint32_t> material_slots;
    std::vector<filament::MaterialInstance *> material_instances;

  private:
    void move_from(MeshRenderableProxy &&other) noexcept {
      entity = other.entity;
      other.entity.clear();
      source_entity = std::exchange(other.source_entity, kInvalidEntityId);
      mesh = std::exchange(other.mesh, kInvalidAssetId);
      material = std::exchange(other.material, kInvalidAssetId);
      mesh_revision = std::exchange(other.mesh_revision, 0);
      material_revision = std::exchange(other.material_revision, 0);
      cast_shadows = std::exchange(other.cast_shadows, false);
      double_sided = std::exchange(other.double_sided, false);
      vertex_buffer = std::exchange(other.vertex_buffer, nullptr);
      index_buffer = std::exchange(other.index_buffer, nullptr);
      material_slots = std::move(other.material_slots);
      material_instances = std::move(other.material_instances);
    }
};

} // namespace quader

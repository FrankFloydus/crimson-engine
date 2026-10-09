////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"
#include "core/types.hpp"

#include <filament/IndexBuffer.h>
#include <filament/InstanceBuffer.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace filament { class SkinningBuffer; }

namespace quader {

struct EditableMeshGeometryState;

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
    MaterialReference material;
    std::uint64_t mesh_revision = 0;
    std::uint64_t material_revision = 0;
    std::uint64_t geometry_resource = 0;
    bool cast_shadows = false;
    bool double_sided = false;
    bool owns_mesh_buffers = true;
    bool owns_material_instances = true;
    bool dynamic_geometry = false;
    /// A singleton instance uses the entity for reflected winding and normals.
    bool entity_transform_instance = false;
    std::uint32_t vertex_count = 0u;
    std::uint32_t index_count = 0u;
    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    filament::InstanceBuffer *instance_buffer = nullptr;
    // The combined geometry owner owns the palette. Render passes borrow it.
    filament::SkinningBuffer *skinning_buffer = nullptr;
    std::shared_ptr<const EditableMeshGeometryState> editable_geometry_state;
    std::vector<std::uint32_t> material_slots;
    std::vector<filament::MaterialInstance *> material_instances;

  private:
    void move_from(MeshRenderableProxy &&other) noexcept {
      entity = other.entity;
      other.entity.clear();
      source_entity = std::exchange(other.source_entity, kInvalidEntityId);
      mesh = std::exchange(other.mesh, kInvalidAssetId);
      material = std::exchange(other.material, MaterialReference{});
      mesh_revision = std::exchange(other.mesh_revision, 0);
      material_revision = std::exchange(other.material_revision, 0);
      geometry_resource = std::exchange(other.geometry_resource, 0);
      cast_shadows = std::exchange(other.cast_shadows, false);
      double_sided = std::exchange(other.double_sided, false);
      owns_mesh_buffers = std::exchange(other.owns_mesh_buffers, true);
      owns_material_instances =
          std::exchange(other.owns_material_instances, true);
      dynamic_geometry = std::exchange(other.dynamic_geometry, false);
      entity_transform_instance = std::exchange(other.entity_transform_instance, false);
      vertex_count = std::exchange(other.vertex_count, 0u);
      index_count = std::exchange(other.index_count, 0u);
      vertex_buffer = std::exchange(other.vertex_buffer, nullptr);
      index_buffer = std::exchange(other.index_buffer, nullptr);
      instance_buffer = std::exchange(other.instance_buffer, nullptr);
      skinning_buffer = std::exchange(other.skinning_buffer, nullptr);
      editable_geometry_state = std::move(other.editable_geometry_state);
      material_slots = std::move(other.material_slots);
      material_instances = std::move(other.material_instances);
    }
};

} // namespace quader

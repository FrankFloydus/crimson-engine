////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_document.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "scene/components.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

/**
 * Captures one render-visible texture asset dependency for a material snapshot.
 */
struct RenderTextureAssetSnapshot {
  std::string slot;
  AssetId texture_id = kInvalidAssetId;
  TextureAsset texture;
  bool present = false;
};

/**
 * Captures material data needed by render material binding without exposing the
 * live asset registry to proxy caches.
 */
struct RenderMaterialAssetSnapshot {
  AssetId material_id = kInvalidAssetId;
  MaterialDocument document;
  std::uint64_t revision = 0;
  bool present = false;
  std::vector<RenderTextureAssetSnapshot> textures;

  [[nodiscard]] const RenderTextureAssetSnapshot *
  texture_for_slot(std::string_view slot) const;
};

/**
 * Captures a scene material component and all material assets it can bind.
 */
struct RenderMaterialBindingSnapshot {
  MaterialComponent component;
  RenderMaterialAssetSnapshot primary_material;
  std::vector<std::pair<std::uint32_t, RenderMaterialAssetSnapshot>>
      material_slots;
  std::uint64_t revision = 0;
  bool present = false;

  [[nodiscard]] const RenderMaterialAssetSnapshot *
  material_for_slot(std::uint32_t slot) const;
};

/**
 * Captures mesh geometry as spans plus explicit identity and revision fields.
 */
struct RenderMeshAssetSnapshot {
  AssetId mesh_id = kInvalidAssetId;
  std::uint64_t mesh_revision = 0;
  std::span<const MeshVertex> vertices;
  std::span<const std::uint32_t> indices;
  std::span<const MeshPrimitiveSpan> primitives;
  bool present = false;
  bool has_stable_cpu_topology = true;
  bool has_authored_polygon_document = false;

  [[nodiscard]] bool renderable() const {
    return present && !vertices.empty() && !indices.empty();
  }
};

/**
 * Describes one scene mesh extracted into a render snapshot.
 */
struct RenderSceneMeshRecord {
  EntityId entity_id = kInvalidEntityId;
  RenderMeshAssetSnapshot mesh;
  MeshComponent mesh_component;
  RenderMaterialBindingSnapshot material;
  AssetId material_id = kInvalidAssetId;
  std::uint64_t material_revision = 0;
  QMat4 world_transform;
  std::uint64_t transform_revision = 0;
  bool mesh_dirty = true;
  bool material_dirty = true;
  bool transform_dirty = true;
  bool binding_dirty = true;

  [[nodiscard]] bool renderable() const { return mesh.renderable(); }
  [[nodiscard]] bool cast_shadows() const { return mesh_component.cast_shadows; }
  [[nodiscard]] bool double_sided() const { return mesh_component.double_sided; }
};

/**
 * Describes one scene light extracted into a render snapshot.
 */
struct RenderSceneLightRecord {
  EntityId entity_id = kInvalidEntityId;
  LightComponent light;
  QMat4 world_transform;
  std::uint64_t rebuild_revision = 0;
  std::uint64_t transform_revision = 0;
  bool light_dirty = true;
  bool transform_dirty = true;
};

/**
 * Describes which scene or asset revision buckets changed since the previous snapshot.
 */
struct RenderSceneSnapshotDirtyState {
  bool scene_changed = true;
  bool selection_changed = true;
  bool visibility_changed = true;
  bool transforms_changed = true;
  bool mesh_bindings_changed = true;
  bool assets_changed = true;

  [[nodiscard]] bool any_dirty() const {
    return scene_changed || selection_changed || visibility_changed ||
           transforms_changed || mesh_bindings_changed || assets_changed;
  }
};

using RenderSelectedObjectOverlaySnapshotRecord =
    RenderOverlaySelectedObjectSnapshot;
using RenderSceneWireframeOverlaySnapshotRecord =
    RenderOverlaySceneWireframeSnapshot;
using RenderHierarchySelectionOverlaySnapshotRecord =
    RenderOverlayMeshSnapshot;
using RenderGroupBoundsOverlaySnapshotRecord = RenderOverlayBoundsSnapshot;
using RenderSelectedLightOverlaySnapshotRecord =
    RenderOverlayLightMarkerSnapshot;
using RenderToolOverlaySnapshotRecord = ViewportOverlayFrame;
using OverlaySnapshot = RenderOverlaySelectionSnapshot;

/**
 * Snapshot consumed by render sync/cache invalidation and rebuild.
 */
struct RenderSceneSnapshot {
  std::uint64_t frame_index = 0;
  std::uint64_t scene_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t visibility_revision = 0;
  std::uint64_t transform_revision = 0;
  std::uint64_t mesh_binding_revision = 0;
  std::uint64_t asset_revision = 0;
  RenderSceneSnapshotDirtyState dirty;
  bool reused_previous_records = false;
  std::vector<RenderSceneMeshRecord> meshes;
  std::vector<RenderSceneLightRecord> lights;
  std::unordered_set<EntityId> live_mesh_entities;
  std::unordered_set<EntityId> live_light_entities;
};

/**
 * Provides frame-boundary inputs for render scene snapshot construction.
 */
struct RenderSceneSnapshotBuildOptions {
  const RenderSceneSnapshot *previous = nullptr;
  std::uint64_t frame_index = 0;
};

/**
 * Captures a tool preview mesh and its render material dependencies.
 */
struct RenderToolPreviewMeshRecord {
  EntityId source_entity = kInvalidEntityId;
  AssetId material_id = kInvalidAssetId;
  MeshAsset mesh;
  RenderMaterialBindingSnapshot material;
  std::uint64_t material_revision = 0;
  QMat4 transform;

  [[nodiscard]] bool renderable() const {
    return !mesh.vertices.empty() && !mesh.indices.empty();
  }
};

} // namespace quader

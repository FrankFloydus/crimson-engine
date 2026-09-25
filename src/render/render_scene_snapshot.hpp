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
#include "render/project_material_resolver.hpp"
#include "render/instance_batch_planner.hpp"
#include "render/render_source_stamp.hpp"
#include "scene/components.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

class CurveSolidGeometryCache;
struct CurveSamplePayload;

/**
 * Captures one render-visible texture asset dependency for a material snapshot.
 */
struct RenderTextureAssetSnapshot {
  std::string slot;
  AssetId texture_id = kInvalidAssetId;
  ProjectAssetId project_texture_id;
  std::uint64_t content_revision = 0u;
  TextureAsset texture;
  std::shared_ptr<const std::vector<std::byte>> encoded_file_bytes;
  std::shared_ptr<const std::vector<std::byte>> decoded_rgba8;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  bool present = false;

  [[nodiscard]] bool project_owned() const noexcept {
    return project_texture_id.valid();
  }
};

/**
 * Captures material data needed by render material binding without exposing the
 * live asset registry to proxy caches.
 */
struct RenderMaterialAssetSnapshot {
  MaterialReference reference;
  AssetId material_id = kInvalidAssetId;
  std::uint64_t project_catalog_revision = 0u;
  MaterialDocument document;
  float normal_strength = 1.0f;
  bool directx_normal = false;
  RenderStandardMaterialTextureTransformSnapshot texture_transform;
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
  std::shared_ptr<const MeshGeometryPayload> geometry_payload;
  std::uintptr_t geometry_token = 0u;
  MeshGeometryContentId geometry_content_id;
  std::span<const MeshVertex> vertices;
  std::span<const std::uint32_t> indices;
  std::span<const MeshPrimitiveSpan> primitives;
  bool present = false;
  bool has_stable_cpu_topology = true;

  [[nodiscard]] bool renderable() const {
    return present && !vertices.empty() && !indices.empty();
  }
};

/// Stores one scene occurrence transform consumed by explicit GPU instancing.
struct RenderSceneInstanceOccurrenceRecord final {
  EntityId entity_id = kInvalidEntityId;
  std::uint64_t occurrence = 0u;
  QMat4 world_transform;
  QMat4 sampling_from_local;
  bool selected = false;
  bool dimmed = false;
};

/**
 * Describes one compatible primitive payload and all of its visible scene
 * occurrences. Definition/member identities are diagnostic only and never
 * participate in the batch key.
 */
struct RenderSceneInstancePrimitiveRecord final {
  EntityId definition_entity = kInvalidEntityId;
  EntityId member_entity = kInvalidEntityId;
  QMat4 member_from_definition;
  RenderInstancePrimitiveKey key;
  RenderMeshAssetSnapshot mesh;
  MeshPrimitiveSpan primitive;
  RenderMaterialBindingSnapshot material;
  RenderInstanceBounds local_bounds;
  std::vector<RenderSceneInstanceOccurrenceRecord> occurrences;

  [[nodiscard]] bool renderable() const {
    return mesh.renderable() && primitive.index_count > 0u &&
           !occurrences.empty();
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
  MaterialReference material_reference;
  AssetId material_id = kInvalidAssetId;
  std::uint64_t material_revision = 0;
  QMat4 world_transform;
  QMat4 sampling_from_local;
  std::uint64_t transform_revision = 0;
  bool mesh_dirty = true;
  bool material_dirty = true;
  bool transform_dirty = true;
  bool texture_mapping_dirty = true;
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
  bool local_visibility_changed = true;
  bool transforms_changed = true;
  bool texture_mappings_changed = true;
  bool mesh_bindings_changed = true;
  bool render_components_changed = true;
  bool assets_changed = true;
  bool project_materials_changed = true;

  [[nodiscard]] bool any_dirty() const {
    return scene_changed || selection_changed || visibility_changed ||
           local_visibility_changed || transforms_changed ||
           texture_mappings_changed ||
           mesh_bindings_changed ||
           render_components_changed || assets_changed ||
           project_materials_changed;
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
struct RenderSceneSnapshot : RenderSourceStamp {
  std::uint64_t frame_index = 0;
  /// Scene-owned proof that existing light properties alone advanced.
  std::uint64_t light_property_revision = 0;
  RenderSceneSnapshotDirtyState dirty;
  bool reused_previous_records = false;
  // Prevents the Curve-publication overlay fast path from retaining stale
  // derived tube geometry when Curve assets or components change.
  bool has_solidified_curves = false;
  std::vector<RenderSceneMeshRecord> meshes;
  std::vector<RenderSceneInstancePrimitiveRecord> instance_primitives;
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
  RenderSceneBindingKey scene_binding;
  const RenderProjectMaterialResolver *project_material_resolver = nullptr;
  std::uint64_t project_material_revision = 0u;
  RenderInstanceEditScope instance_edit_scope;
  const ViewportLocalVisibilitySnapshot *local_visibility = nullptr;
  CurveSolidGeometryCache *curve_solid_geometry_cache = nullptr;
};

/**
 * Captures a tool preview mesh and its render material dependencies.
 */
struct RenderToolPreviewMeshRecord {
  int source_object_id = -1;
  EntityId source_entity = kInvalidEntityId;
  // A preview sourced from a canonical instance-definition member is rendered
  // in one public occurrence. Retain that occurrence so source suppression
  // does not hide every other occurrence of the same definition member.
  EntityId visibility_entity = kInvalidEntityId;
  MaterialReference material_reference;
  AssetId material_id = kInvalidAssetId;
  MeshAsset mesh;
  RenderMaterialBindingSnapshot material;
  std::uint64_t material_revision = 0;
  QMat4 transform;
  std::vector<QMat4> instance_transforms;
  QMat4 sampling_from_local;
  std::vector<QMat4> instance_sampling_from_local;
  // Material-instance culling policy retained with the preview snapshot.
  bool double_sided = true;
  bool cast_shadows = false;
  bool dynamic_positions = false;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
  // Participant-local token. Equal identity, revision, and payload ownership
  // mean the retained render group does not need a position upload.
  std::uint64_t position_revision = 0u;
  std::shared_ptr<const std::vector<QVec3>> dynamic_position_payload;

  [[nodiscard]] bool renderable() const {
    return !mesh.geometry().vertices.empty() &&
           !mesh.geometry().indices.empty();
  }
};

/// Suppresses one canonical member/definition only in one public occurrence.
struct RenderInstancePreviewSuppression final {
  EntityId source_entity = kInvalidEntityId;
  EntityId occurrence = kInvalidEntityId;

  friend bool operator==(const RenderInstancePreviewSuppression &,
                         const RenderInstancePreviewSuppression &) = default;
};

/** Pins one authored Curve sample payload and every preview placement. */
struct RenderToolPreviewCurveRecord final {
  EntityId source_entity = kInvalidEntityId;
  EntityId visibility_entity = kInvalidEntityId;
  AssetId curve = kInvalidAssetId;
  std::uint64_t curve_revision = 0u;
  std::shared_ptr<const CurveSamplePayload> sample_payload;
  MaterialComponent material;
  std::vector<QMat4> instance_transforms;
  bool solidify = false;
  int solidify_resolution = kCurveSolidifyDefaultResolution;
  float solidify_radius = static_cast<float>(kCurveSolidifyDefaultRadius);
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
  /// Exact authored bounds, including solidification, across preview placements.
  std::optional<ViewportSelectionDimensionBounds> selection_dimension_bounds;
};

/** Owns one transient Light descriptor and every preview placement. */
struct RenderToolPreviewLightRecord final {
  EntityId source_entity = kInvalidEntityId;
  EntityId visibility_entity = kInvalidEntityId;
  LightComponent light;
  std::vector<QMat4> instance_transforms;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
};

/** Repeats one source-root-local bounds cage at preview placements. */
struct RenderToolPreviewGroupMarkerRecord final {
  EntityId source_entity = kInvalidEntityId;
  QVec3 local_min;
  QVec3 local_max;
  std::vector<QMat4> instance_transforms;
  std::uint64_t layout_key = 0u;
};

/** Exact preview-only source coverage shared by render and hit-policy adapters. */
struct RenderToolPreviewSuppressionSnapshot final {
  std::vector<EntityId> entities;
  std::vector<RenderInstancePreviewSuppression> occurrences;
  std::uint64_t signature = 0u;

  [[nodiscard]] bool empty() const noexcept {
    return entities.empty() && occurrences.empty();
  }
};

/** One atomically published compound tool preview generation. */
struct RenderToolPreviewSceneSnapshot final {
  // False means extraction could not pin every requested participant. Render
  // publication rejects the complete candidate and retains the prior scene.
  bool complete = true;
  std::uint64_t revision = 0u;
  std::vector<RenderToolPreviewMeshRecord> meshes;
  std::vector<RenderToolPreviewCurveRecord> curves;
  std::vector<RenderToolPreviewLightRecord> lights;
  std::vector<RenderToolPreviewGroupMarkerRecord> group_markers;
  RenderToolPreviewSuppressionSnapshot suppression;
};

} // namespace quader

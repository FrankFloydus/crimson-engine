////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "render/render_scene_binding.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/overlay/viewport_persistent_overlay_style.hpp"
#include "scene/components.hpp"
#include "viewport/viewport_camera_clip_planes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace quader_poly {
struct Face;
struct Vertex;
} // namespace quader_poly

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Captures a render-bound mesh overlay input without exposing scene/asset owner
 * objects to overlay builders.
 */
struct RenderOverlayMeshSnapshot {
  EntityId entity = kInvalidEntityId;
  /** Distinguishes expanded definition members that share one occurrence. */
  std::uint64_t participant_token = 0u;
  AssetId mesh_id = kInvalidAssetId;
  std::uint64_t mesh_revision = 0;
  std::shared_ptr<const MeshGeometryPayload> geometry_payload;
  MeshGeometryContentId geometry_content_id;
  std::shared_ptr<const quader_poly::Document> authored_topology_payload;
  QMat4 world_transform;
  bool selected = false;
  std::span<const MeshVertex> vertices;
  std::span<const std::uint32_t> indices;
  std::span<const quader_poly::Vertex> authored_vertices;
  std::span<const quader_poly::Face> authored_faces;

  [[nodiscard]] bool has_triangle_geometry() const {
    return !vertices.empty() && indices.size() >= 3u;
  }

  [[nodiscard]] bool has_authored_polygon_geometry() const {
    return !authored_vertices.empty() && !authored_faces.empty();
  }
};

/// Identifies transient selected-instance overlay records by visual purpose.
enum class RenderOverlaySemantic : std::uint8_t {
  InstanceSelectionFill,
  InstanceSelectionWire,
};

/// Carries selected-occurrence fill state without Filament resource handles.
struct RenderOverlayInstanceSelectionFillRecord final {
  RenderOverlaySemantic semantic =
      RenderOverlaySemantic::InstanceSelectionFill;
  EntityId occurrence = kInvalidEntityId;
  EntityId definition = kInvalidEntityId;
  EntityId member = kInvalidEntityId;
  AssetId mesh_id = kInvalidAssetId;
  std::uint64_t mesh_revision = 0u;
  std::shared_ptr<const MeshGeometryPayload> geometry_payload;
  MeshGeometryContentId geometry_content_id;
  QMat4 world_transform;
  std::span<const MeshVertex> vertices;
  std::span<const std::uint32_t> indices;
  std::uint32_t color = kOverlayInstanceSelectionFillColor;
  bool depth_test = true;
  bool depth_write = false;
  float polygon_offset_scale = -1.0f;
  float polygon_offset_constant = -1.0f;
  std::uint16_t blend_order = 0u;
  ViewportOverlayLayer draws_before = ViewportOverlayLayer::SourceWire;
};

/// Carries the crisp selected-occurrence wire drawn after its translucent fill.
struct RenderOverlayInstanceSelectionWireRecord final {
  RenderOverlaySemantic semantic =
      RenderOverlaySemantic::InstanceSelectionWire;
  EntityId occurrence = kInvalidEntityId;
  EntityId definition = kInvalidEntityId;
  EntityId member = kInvalidEntityId;
  AssetId mesh_id = kInvalidAssetId;
  std::uint64_t mesh_revision = 0u;
  std::shared_ptr<const MeshGeometryPayload> geometry_payload;
  MeshGeometryContentId geometry_content_id;
  std::shared_ptr<const quader_poly::Document> authored_topology_payload;
  QMat4 world_transform;
  std::span<const MeshVertex> vertices;
  std::span<const std::uint32_t> indices;
  std::span<const quader_poly::Vertex> authored_vertices;
  std::span<const quader_poly::Face> authored_faces;
  std::uint32_t color = kOverlaySelectionWireColor;
  float line_width_pixels = kOverlaySelectionWireLineWidthPixels;
  std::uint16_t blend_order = static_cast<std::uint16_t>(7u * 64u);
  ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire;

  [[nodiscard]] bool has_authored_polygon_geometry() const {
    return !authored_vertices.empty() && !authored_faces.empty();
  }
};

/**
 * Stores one selected group or hierarchy bounds cage in world space.
 */
struct RenderOverlayBoundsSnapshot {
  QVec3 min;
  QVec3 max;
};

/**
 * Stores one selected non-billboard light marker input for render overlay
 * projection.
 */
struct RenderOverlayLightMarkerSnapshot {
  EntityId entity = kInvalidEntityId;
  LightType type = LightType::Directional;
  QMat4 world_transform;
};

/**
 * Captures persistent selected-object overlay inputs and the dirty revisions
 * needed by the cache.
 */
struct RenderOverlaySelectedObjectSnapshot {
  RenderSceneBindingKey scene_binding;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t visibility_revision = 0;
  std::uint64_t transform_revision = 0;
  std::uint64_t mesh_binding_revision = 0;
  ViewportPersistentOverlayStyleSnapshot style;
  ViewportCameraClipPlanes clip_planes;
  bool visible = true;
  std::vector<RenderOverlayMeshSnapshot> meshes;
  std::vector<RenderOverlayInstanceSelectionFillRecord>
      instance_selection_fills;
  std::vector<RenderOverlayInstanceSelectionWireRecord>
      instance_selection_wires;
};

/**
 * Captures persistent wireframe overlay inputs and the dirty revisions needed
 * by the cache.
 */
struct RenderOverlaySceneWireframeSnapshot {
  RenderSceneBindingKey scene_binding;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  ViewportPersistentOverlayStyleSnapshot style;
  ViewportCameraClipPlanes clip_planes;
  std::uint8_t layer_mask = 0u;
  std::vector<RenderOverlayMeshSnapshot> meshes;
};

/**
 * Captures transient scene/tool overlay records appended to the runtime overlay
 * frame before style resolution.
 */
struct RenderOverlaySelectionSnapshot {
  RenderSceneBindingKey scene_binding;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t visibility_revision = 0;
  std::uint64_t transform_revision = 0;
  std::uint64_t mesh_binding_revision = 0;
  std::uint64_t render_component_revision = 0;
  std::vector<RenderOverlayMeshSnapshot> hierarchy_selection_wires;
  std::vector<RenderOverlayBoundsSnapshot> group_bounds;
  std::vector<RenderOverlayBoundsSnapshot> selected_mesh_bounds;
  std::vector<RenderOverlayLightMarkerSnapshot> selected_lights;
  std::vector<RenderOverlayLightMarkerSnapshot> point_light_gizmos;
  ViewportOverlayFrame tool_overlays;
};

/**
 * References the persistent overlay snapshots prepared for one render frame.
 * Pointers remain valid until the owning cache is reset or rebound.
 */
struct RenderOverlayPersistentSnapshotUpdate {
  const RenderOverlaySelectedObjectSnapshot *selected_object = nullptr;
  const RenderOverlaySceneWireframeSnapshot *scene_wireframe = nullptr;
  bool selected_object_changed = false;
  bool scene_wireframe_changed = false;

  [[nodiscard]] bool changed() const {
    return selected_object_changed || scene_wireframe_changed;
  }
};

/**
 * Records source-derived rebuilds for each overlay snapshot branch. Cache hits
 * and metadata-only refreshes do not increment these counters, allowing tests
 * to prove that camera-only frames remain extraction-free.
 */
struct RenderOverlaySnapshotCacheStats {
  std::uint64_t selected_object_build_count = 0;
  std::uint64_t scene_wireframe_build_count = 0;
  std::uint64_t selection_build_count = 0;
  std::uint64_t selection_local_bounds_build_count = 0;
  std::uint64_t selection_local_bounds_reuse_count = 0;
  std::uint64_t selection_world_bounds_build_count = 0;
  std::uint64_t selection_world_bounds_translation_reuse_count = 0;
  std::uint64_t selection_transform_refresh_count = 0;
};

/**
 * Retains renderer-neutral overlay extraction across camera-only frames.
 * Snapshot mesh spans borrow from the bound AssetRegistry, which must outlive
 * cache use.
 */
class RenderOverlaySnapshotCache final {
public:
  [[nodiscard]] RenderOverlayPersistentSnapshotUpdate update_persistent(
      const SceneDocument &document, const AssetRegistry &assets,
      bool selected_object_overlay_visible,
      std::uint8_t scene_wireframe_layer_mask,
      ViewportPersistentOverlayStyleSnapshot style,
      RenderSceneBindingKey scene_binding = {});
  [[nodiscard]] const RenderOverlaySelectionSnapshot &selection_snapshot(
      const SceneDocument &document, const AssetRegistry &assets,
      RenderSceneBindingKey scene_binding = {});

  void reset();

  [[nodiscard]] const RenderOverlaySnapshotCacheStats &stats() const {
    return stats_;
  }

private:
  /// Retains asset-local bounds until the source mesh revision changes.
  struct CachedLocalMeshBounds {
    std::uint64_t mesh_revision = 0;
    bool has_bounds = false;
    RenderOverlayBoundsSnapshot bounds;
  };

  /// Retains one entity's world bounds for exact transform-only refreshes.
  struct CachedWorldMeshBounds {
    AssetId mesh_id = kInvalidAssetId;
    std::uint64_t mesh_revision = 0;
    bool has_mesh_asset = false;
    QMat4 world_transform;
    RenderOverlayBoundsSnapshot bounds;
  };

  void bind_sources(const SceneDocument &document,
                    const AssetRegistry &assets,
                    RenderSceneBindingKey scene_binding);
  void invalidate_snapshots();
  [[nodiscard]] std::optional<RenderOverlayBoundsSnapshot>
  selected_mesh_world_bounds(const SceneDocument &document,
                             const AssetRegistry &assets, EntityId entity);
  [[nodiscard]] std::optional<RenderOverlayBoundsSnapshot>
  refresh_selected_mesh_world_bounds(const SceneDocument &document,
                                     EntityId entity);
  [[nodiscard]] bool can_refresh_selection_transforms(
      const SceneDocument &document, const AssetRegistry &assets) const;
  [[nodiscard]] bool refresh_selection_transforms(
      const SceneDocument &document, const AssetRegistry &assets);

  const SceneDocument *source_document_ = nullptr;
  const AssetRegistry *source_assets_ = nullptr;
  RenderSceneBindingKey source_scene_binding_;
  RenderOverlaySelectedObjectSnapshot selected_object_snapshot_;
  RenderOverlaySceneWireframeSnapshot scene_wireframe_snapshot_;
  RenderOverlaySelectionSnapshot selection_snapshot_;
  bool has_selected_object_snapshot_ = false;
  bool has_scene_wireframe_snapshot_ = false;
  bool has_selection_snapshot_ = false;
  std::unordered_map<AssetId, CachedLocalMeshBounds>
      selection_local_mesh_bounds_;
  std::unordered_map<EntityId, CachedWorldMeshBounds>
      selection_world_mesh_bounds_;
  std::vector<EntityId> selection_selected_mesh_entities_;
  std::vector<EntityId> selection_group_entities_;
  RenderOverlaySnapshotCacheStats stats_;
};

[[nodiscard]] RenderOverlaySelectedObjectSnapshot
build_render_overlay_selected_object_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              bool visible,
                                              RenderSceneBindingKey
                                                  scene_binding = {});
[[nodiscard]] RenderOverlaySelectedObjectSnapshot
build_render_overlay_selected_object_snapshot(
    const SceneDocument &document, const AssetRegistry &assets, bool visible,
    ViewportPersistentOverlayStyleSnapshot style,
    RenderSceneBindingKey scene_binding = {});

[[nodiscard]] RenderOverlaySceneWireframeSnapshot
build_render_overlay_scene_wireframe_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              std::uint8_t layer_mask,
                                              RenderSceneBindingKey
                                                  scene_binding = {});
[[nodiscard]] RenderOverlaySceneWireframeSnapshot
build_render_overlay_scene_wireframe_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    std::uint8_t layer_mask, ViewportPersistentOverlayStyleSnapshot style,
    RenderSceneBindingKey scene_binding = {});

[[nodiscard]] RenderOverlaySelectionSnapshot
build_render_overlay_selection_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    ViewportOverlayFrame tool_overlays = {},
    RenderSceneBindingKey scene_binding = {});

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/instance_batch_planner.hpp"
#include "render/overlay/instanced_wire_render_backend.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace filament {
class Engine;
class Material;
class Scene;
} // namespace filament

namespace quader {

[[nodiscard]] constexpr bool scene_wireframe_style_changed(
    bool has_synced, std::uint64_t synced_signature,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  return !has_synced || synced_signature != style.wire_signature;
}

/// Reports deterministic renderer-neutral work performed by one batch prepare.
struct SceneWireframeOverlayPlanWorkStats final {
  std::size_t primitive_record_index_entry_count = 0u;
  std::size_t chunk_record_lookup_count = 0u;
  std::size_t local_bounds_cache_hit_count = 0u;
  std::uint64_t local_bounds_vertex_visit_count = 0u;
};

/// Carries the renderer-neutral full-scene wire plan and sharing counts.
struct SceneWireframeOverlayBatchPlan final {
  RenderInstanceBatchPlan batches;
  std::size_t participant_count = 0u;
  std::size_t unique_geometry_style_count = 0u;
  SceneWireframeOverlayPlanWorkStats work;
};

/**
 * Groups ordinary meshes and expanded formal-instance members by immutable
 * topology/style, then chunks each compatible group at the backend limit.
 */
[[nodiscard]] SceneWireframeOverlayBatchPlan
plan_scene_wireframe_overlay_batches(
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    std::size_t max_instances_per_chunk,
    const RenderInstanceBatchPlan *retained_layout = nullptr);

/// Compares pinned mesh inputs without allocating or inspecting mesh vertices.
/// Scene/asset revisions may advance for unrelated curves, lights or materials.
[[nodiscard]] bool scene_wireframe_overlay_mesh_inputs_match(
    std::span<const RenderOverlayMeshSnapshot> previous,
    std::span<const RenderOverlayMeshSnapshot> next);

/// Reports resources and submissions for the persistent full-scene wire path.
struct SceneWireframeOverlayCacheStats final {
  std::size_t participant_count = 0u;
  std::size_t chunk_count = 0u;
  std::size_t raster_group_count = 0u;
  std::size_t unique_geometry_resource_count = 0u;
  std::uint64_t derived_vertex_bytes = 0u;
  std::uint64_t derived_index_bytes = 0u;
  std::uint64_t geometry_upload_count = 0u;
  std::uint64_t geometry_reuse_count = 0u;
  std::uint64_t line_width_update_count = 0u;
  std::uint64_t rebuild_count = 0u;
  std::uint64_t transform_update_count = 0u;
  std::uint64_t plan_prepare_count = 0u;
  std::uint64_t mesh_input_reuse_count = 0u;
};

/**
 * Owns explicit Filament instance batches for the persistent scene wireframe.
 * Identical immutable topology/style inputs share one derived local edge-ribbon
 * VB/IB; participant transforms live only in device-cap-sized InstanceBuffers.
 */
class SceneWireframeOverlayCache final {
public:
  SceneWireframeOverlayCache();
  SceneWireframeOverlayCache(const SceneWireframeOverlayCache &) = delete;
  SceneWireframeOverlayCache &
  operator=(const SceneWireframeOverlayCache &) = delete;
  ~SceneWireframeOverlayCache();

  void initialize(filament::Engine &engine, filament::Scene &scene,
                  filament::Material &line_material,
                  std::size_t max_instances_per_chunk);
  void shutdown();
  void clear();
  void sync(const RenderOverlaySceneWireframeSnapshot &snapshot);

  [[nodiscard]] bool initialized() const {
    return wire_backend_.initialized();
  }

  [[nodiscard]] const SceneWireframeOverlayCacheStats &stats() const {
    return stats_;
  }

  [[nodiscard]] std::size_t max_instances_per_chunk() const {
    return max_instances_per_chunk_;
  }

private:
  struct PreparedPlan;
  using WireBatchProxy = InstancedWireRenderBackend::BatchProxy;

  /** Per-pane-style proxies that share the cache's immutable wire geometry. */
  struct RasterProxyGroup final {
    std::uint8_t layer_mask = 0u;
    ViewportPersistentOverlayStyleSnapshot style;
    std::vector<WireBatchProxy> proxies;
  };

  [[nodiscard]] PreparedPlan prepare(
      const RenderOverlaySceneWireframeSnapshot &snapshot) const;
  bool rebuild(const PreparedPlan &prepared,
               const RenderOverlaySceneWireframeSnapshot &snapshot);
  bool update_transforms(const PreparedPlan &prepared);
  bool reconcile_raster_groups(
      const PreparedPlan &prepared,
      const RenderOverlaySceneWireframeSnapshot &snapshot,
      const ViewportPersistentOverlayRasterGroupSet &raster_groups);
  [[nodiscard]] bool create_raster_group(
      const PreparedPlan &prepared,
      const RenderOverlaySceneWireframeSnapshot &snapshot,
      const ViewportPersistentOverlayRasterGroup &raster_group,
      RasterProxyGroup &result);
  [[nodiscard]] bool update_raster_group(
      const PreparedPlan &prepared,
      const RenderOverlaySceneWireframeSnapshot &snapshot,
      const ViewportPersistentOverlayRasterGroup &raster_group,
      RasterProxyGroup &proxy_group);
  void destroy_raster_groups(std::span<RasterProxyGroup> proxy_groups);
  void prune_unused_wire_geometry();
  void refresh_wire_resource_stats();
  void update_clip_planes(ViewportCameraClipPlanes clip_planes);
  void update_instance_edit_style(const InstanceEditStyle &style);
  void set_all_layer_masks(std::uint8_t layer_mask);

  InstancedWireRenderBackend wire_backend_;
  std::size_t max_instances_per_chunk_ = 1u;
  RenderInstanceBatchPlan plan_;
  /// Pins the exact authored inputs from the last successful synchronization.
  std::vector<RenderOverlayMeshSnapshot> synced_meshes_;
  std::vector<RasterProxyGroup> raster_proxy_groups_;
  ViewportPersistentOverlayRasterGroupSet synced_raster_groups_;
  RenderSourceStamp synced_source_stamp_;
  std::uint64_t synced_wire_style_signature_ = 0u;
  ViewportPersistentOverlayStyleSnapshot synced_wire_style_;
  std::uint64_t synced_instance_edit_context_signature_ = 0u;
  bool synced_selected_mesh_wire_visible_ = true;
  ViewportCameraClipPlanes synced_clip_planes_;
  std::uint8_t synced_layer_mask_ = 0u;
  bool layer_masks_forced_hidden_ = false;
  bool has_synced_ = false;
  SceneWireframeOverlayCacheStats stats_;
};

} // namespace quader

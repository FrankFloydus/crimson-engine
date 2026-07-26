////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/instance_batch_planner.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
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
    std::size_t max_instances_per_chunk);

/// Reports resources and submissions for the persistent full-scene wire path.
struct SceneWireframeOverlayCacheStats final {
  std::size_t participant_count = 0u;
  std::size_t chunk_count = 0u;
  std::size_t unique_geometry_resource_count = 0u;
  std::uint64_t derived_vertex_bytes = 0u;
  std::uint64_t derived_index_bytes = 0u;
  std::uint64_t geometry_upload_count = 0u;
  std::uint64_t geometry_reuse_count = 0u;
  std::uint64_t rebuild_count = 0u;
  std::uint64_t transform_update_count = 0u;
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
    return engine_ != nullptr && scene_ != nullptr &&
           line_material_ != nullptr;
  }

  [[nodiscard]] const SceneWireframeOverlayCacheStats &stats() const {
    return stats_;
  }

  [[nodiscard]] std::size_t max_instances_per_chunk() const {
    return max_instances_per_chunk_;
  }

private:
  struct PreparedPlan;
  struct WireGeometryResource;
  struct WireBatchProxy;

  [[nodiscard]] PreparedPlan prepare(
      const RenderOverlaySceneWireframeSnapshot &snapshot) const;
  bool rebuild(const PreparedPlan &prepared,
               const RenderOverlaySceneWireframeSnapshot &snapshot);
  bool update_transforms(const PreparedPlan &prepared);
  WireGeometryResource *acquire_geometry(
      const RenderOverlayMeshSnapshot &mesh,
      const ViewportPersistentOverlayStyleSnapshot &style);
  WireBatchProxy create_batch(
      const RenderOverlayMeshSnapshot &mesh,
      const RenderInstanceBatchChunk &chunk,
      const ViewportPersistentOverlayStyleSnapshot &style,
      ViewportCameraClipPlanes clip_planes);
  bool update_batch(WireBatchProxy &proxy,
                    const RenderInstanceBatchChunk &chunk);
  void destroy_batch(WireBatchProxy &proxy);
  void destroy_geometry_resources();
  void prune_unused_geometry_resources();
  void update_clip_planes(ViewportCameraClipPlanes clip_planes);
  void set_layer_mask(std::uint8_t layer_mask);

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Material *line_material_ = nullptr;
  std::size_t max_instances_per_chunk_ = 1u;
  RenderInstanceBatchPlan plan_;
  std::vector<WireBatchProxy> proxies_;
  std::unordered_map<std::uint64_t,
                     std::vector<std::unique_ptr<WireGeometryResource>>>
      geometry_resources_;
  RenderSceneBindingKey synced_scene_binding_;
  std::uint64_t synced_scene_revision_ = 0u;
  std::uint64_t synced_asset_revision_ = 0u;
  std::uint64_t synced_local_visibility_revision_ = 0u;
  std::uint64_t synced_wire_style_signature_ = 0u;
  ViewportCameraClipPlanes synced_clip_planes_;
  std::uint8_t layer_mask_ = 0u;
  bool has_synced_ = false;
  SceneWireframeOverlayCacheStats stats_;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/instance_batch_planner.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <utils/Entity.h>

namespace filament {
class Engine;
class IndexBuffer;
class InstanceBuffer;
class Material;
class MaterialInstance;
class Scene;
class VertexBuffer;
} // namespace filament

namespace quader {

class MeshRenderableFactory;

/// Reports deterministic renderer-neutral work performed by one batch prepare.
struct InstanceSelectionOverlayPlanWorkStats final {
  std::size_t fill_record_index_entry_count = 0u;
  std::size_t wire_record_index_entry_count = 0u;
  std::size_t fill_chunk_record_lookup_count = 0u;
  std::size_t wire_chunk_record_lookup_count = 0u;
  std::size_t local_bounds_cache_hit_count = 0u;
  std::uint64_t local_bounds_vertex_visit_count = 0u;
};

/// Groups renderer-neutral selected-instance fill and wire batch plans.
struct InstanceSelectionOverlayBatchPlans final {
  RenderInstanceBatchPlan fills;
  RenderInstanceBatchPlan wires;
  InstanceSelectionOverlayPlanWorkStats work;
};

/// Stores one renderer-neutral segment for an instance selection wire.
struct InstanceSelectionWireEdge final {
  QVec3 start;
  QVec3 end;
};

/**
 * Copies the asset-owned semantic wire projection for cache upload.
 */
[[nodiscard]] std::vector<InstanceSelectionWireEdge>
instance_selection_wire_edges(
    const RenderOverlayInstanceSelectionWireRecord &record);

[[nodiscard]] InstanceSelectionOverlayBatchPlans
plan_instance_selection_overlay_batches(
    const RenderOverlaySelectedObjectSnapshot &snapshot,
    std::size_t max_instances_per_chunk);

/// Reports render-owned selected-instance overlay submissions and resources.
struct InstanceSelectionOverlayCacheStats final {
  std::size_t fill_chunk_count = 0u;
  std::size_t wire_chunk_count = 0u;
  std::size_t selected_member_count = 0u;
  std::uint64_t rebuild_count = 0u;
  std::uint64_t transform_update_count = 0u;
  std::size_t unique_wire_geometry_resource_count = 0u;
  std::uint64_t wire_derived_vertex_bytes = 0u;
  std::uint64_t wire_derived_index_bytes = 0u;
  std::uint64_t wire_geometry_upload_count = 0u;
  std::uint64_t wire_geometry_reuse_count = 0u;
};

/**
 * Owns selected-occurrence fill and crisp wire submissions. Triangle fill
 * geometry is borrowed from MeshRenderableFactory's immutable per-Engine
 * cache; only the derived line-quad geometry has a dedicated resident cache.
 */
class InstanceSelectionOverlayCache final {
public:
  InstanceSelectionOverlayCache();
  InstanceSelectionOverlayCache(const InstanceSelectionOverlayCache &) =
      delete;
  InstanceSelectionOverlayCache &
  operator=(const InstanceSelectionOverlayCache &) = delete;
  ~InstanceSelectionOverlayCache();

  void initialize(filament::Engine &engine, filament::Scene &scene,
                  MeshRenderableFactory &mesh_factory,
                  filament::Material &fill_material,
                  filament::Material &line_material,
                  std::size_t max_instances_per_chunk);
  void shutdown();
  void clear(bool release_geometry_resources = false);
  bool sync(const RenderOverlaySelectedObjectSnapshot &snapshot);

  [[nodiscard]] bool initialized() const {
    return engine_ != nullptr && scene_ != nullptr && mesh_factory_ != nullptr &&
           fill_material_ != nullptr && line_material_ != nullptr;
  }

  [[nodiscard]] const InstanceSelectionOverlayCacheStats &stats() const {
    return stats_;
  }

private:
  struct PreparedPlans;
  struct WireGeometryResource;
  struct WireBatchProxy;

  [[nodiscard]] PreparedPlans prepare(
      const RenderOverlaySelectedObjectSnapshot &snapshot) const;
  bool rebuild(const PreparedPlans &prepared,
               const RenderOverlaySelectedObjectSnapshot &snapshot);
  bool update_transforms(const PreparedPlans &prepared);
  WireGeometryResource *acquire_wire_geometry(
      const RenderOverlayInstanceSelectionWireRecord &record);
  WireBatchProxy create_wire_batch(
      const RenderOverlayInstanceSelectionWireRecord &record,
      const RenderInstanceBatchChunk &chunk,
      ViewportCameraClipPlanes clip_planes);
  bool update_wire_batch(WireBatchProxy &proxy,
                         const RenderInstanceBatchChunk &chunk);
  void destroy_wire_batch(WireBatchProxy &proxy);
  void destroy_wire_resources();
  void prune_unused_wire_resources();
  void update_wire_clip_planes(ViewportCameraClipPlanes clip_planes);

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  MeshRenderableFactory *mesh_factory_ = nullptr;
  filament::Material *fill_material_ = nullptr;
  filament::Material *line_material_ = nullptr;
  std::size_t max_instances_per_chunk_ = 1u;
  RenderInstanceBatchPlan fill_plan_;
  RenderInstanceBatchPlan wire_plan_;
  std::vector<MeshRenderableProxy> fill_proxies_;
  std::vector<WireBatchProxy> wire_proxies_;
  std::unordered_map<std::uint64_t,
                     std::vector<std::unique_ptr<WireGeometryResource>>>
      wire_resources_;
  ViewportCameraClipPlanes clip_planes_;
  std::uint64_t synced_local_visibility_revision_ = 0u;
  InstanceSelectionOverlayCacheStats stats_;
};

} // namespace quader

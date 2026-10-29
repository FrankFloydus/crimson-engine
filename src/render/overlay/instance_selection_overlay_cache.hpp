////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "render/overlay/instance_selection_overlay_plan.hpp"

#include "render/instance_batch_planner.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/overlay/instanced_wire_render_backend.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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

/// Reports render-owned selected-instance overlay submissions and resources.
struct InstanceSelectionOverlayCacheStats final {
  std::size_t fill_chunk_count = 0u;
  std::size_t wire_chunk_count = 0u;
  std::size_t wire_raster_group_count = 0u;
  std::size_t selected_member_count = 0u;
  std::uint64_t rebuild_count = 0u;
  std::uint64_t transform_update_count = 0u;
  std::size_t unique_wire_geometry_resource_count = 0u;
  std::uint64_t wire_derived_vertex_bytes = 0u;
  std::uint64_t wire_derived_index_bytes = 0u;
  std::uint64_t wire_geometry_upload_count = 0u;
  std::uint64_t wire_geometry_reuse_count = 0u;
  std::uint64_t wire_line_width_update_count = 0u;
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
    return wire_backend_.initialized() && mesh_factory_ != nullptr &&
           fill_material_ != nullptr;
  }

  [[nodiscard]] const InstanceSelectionOverlayCacheStats &stats() const {
    return stats_;
  }

private:
  struct PreparedPlans;
  using WireBatchProxy = InstancedWireRenderBackend::BatchProxy;

  /** Per-pane-style proxies that share the cache's immutable wire geometry. */
  struct WireRasterProxyGroup final {
    std::uint8_t layer_mask = 0u;
    ViewportPersistentOverlayStyleSnapshot style;
    std::vector<WireBatchProxy> proxies;
  };

  [[nodiscard]] PreparedPlans prepare(
      const RenderOverlaySelectedObjectSnapshot &snapshot) const;
  bool rebuild(const PreparedPlans &prepared,
               const RenderOverlaySelectedObjectSnapshot &snapshot);
  bool update_transforms(const PreparedPlans &prepared);
  bool reconcile_wire_raster_groups(
      const PreparedPlans &prepared,
      const ViewportPersistentOverlayRasterGroupSet &raster_groups,
      ViewportCameraClipPlanes clip_planes);
  [[nodiscard]] bool create_wire_raster_group(
      const PreparedPlans &prepared,
      const ViewportPersistentOverlayRasterGroup &raster_group,
      ViewportCameraClipPlanes clip_planes,
      WireRasterProxyGroup &result);
  [[nodiscard]] bool update_wire_raster_group(
      const PreparedPlans &prepared,
      const ViewportPersistentOverlayRasterGroup &raster_group,
      WireRasterProxyGroup &proxy_group);
  void destroy_wire_raster_groups(
      std::span<WireRasterProxyGroup> proxy_groups);
  void prune_unused_wire_geometry();
  void refresh_wire_resource_stats();
  void update_wire_clip_planes(ViewportCameraClipPlanes clip_planes);
  bool update_fill_colors(const ViewportPersistentOverlayStyleSnapshot &style);

  InstancedWireRenderBackend wire_backend_;
  MeshRenderableFactory *mesh_factory_ = nullptr;
  filament::Material *fill_material_ = nullptr;
  std::size_t max_instances_per_chunk_ = 1u;
  RenderInstanceBatchPlan fill_plan_;
  RenderInstanceBatchPlan wire_plan_;
  std::vector<MeshRenderableProxy> fill_proxies_;
  std::vector<WireRasterProxyGroup> wire_proxy_groups_;
  ViewportPersistentOverlayRasterGroupSet synced_raster_groups_;
  ViewportCameraClipPlanes clip_planes_;
  RenderSourceStamp synced_source_stamp_;
  std::uint64_t synced_wire_style_signature_ = 0u;
  std::uint32_t synced_active_color_ = 0u;
  bool synced_selected_mesh_outline_only_ = false;
  bool has_synced_ = false;
  InstanceSelectionOverlayCacheStats stats_;
};

} // namespace quader

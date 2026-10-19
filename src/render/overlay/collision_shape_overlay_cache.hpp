////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/collision_shape_overlay_planner.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/overlay/instanced_wire_render_backend.hpp"

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

class MeshRenderableFactory;

/// Reports retained collision fill resources and backend changes.
struct CollisionShapeOverlayCacheStats final {
  std::size_t fill_chunk_count = 0u;
  std::size_t member_count = 0u;
  std::uint64_t rebuild_count = 0u;
  std::uint64_t transform_update_count = 0u;
  std::uint64_t color_update_count = 0u;
  std::size_t wire_chunk_count = 0u;
};

/// Owns collision fill placements using the factory's shared triangle buffers.
class CollisionShapeOverlayCache final {
public:
  CollisionShapeOverlayCache() = default;
  CollisionShapeOverlayCache(const CollisionShapeOverlayCache &) = delete;
  CollisionShapeOverlayCache &operator=(const CollisionShapeOverlayCache &) =
      delete;
  ~CollisionShapeOverlayCache();

  void initialize(filament::Engine &engine, filament::Scene &scene,
                  MeshRenderableFactory &mesh_factory,
                  filament::Material &fill_material,
                  filament::Material &line_material,
                  std::size_t max_instances_per_chunk);
  void shutdown();
  void clear();
  bool sync(const RenderOverlayCollisionSnapshot &snapshot,
            const RenderOverlaySelectedObjectSnapshot &presentation = {});

  [[nodiscard]] bool initialized() const {
    return mesh_factory_ != nullptr && fill_material_ != nullptr;
  }
  [[nodiscard]] const CollisionShapeOverlayCacheStats &stats() const {
    return stats_;
  }
  [[nodiscard]] const CollisionShapeOverlayPlanWorkStats &plan_stats() const {
    return planner_.stats();
  }

private:
  /** One pane raster group retains both selected and unselected wire chunks. */
  struct WireRasterGroup final {
    std::uint8_t layer_mask = 0u;
    float raster_scale = 1.0f;
    std::vector<InstancedWireRenderBackend::BatchProxy> proxies;
  };
  bool rebuild(const ViewportSettingColor &color);
  bool update_transforms();
  void destroy_proxies();
  bool sync_wires(const RenderOverlayCollisionSnapshot &snapshot,
                  const RenderOverlaySelectedObjectSnapshot &presentation,
                  const CollisionShapeOverlayUpdate &update);
  void destroy_wires();
  void prune_wire_resources();

  MeshRenderableFactory *mesh_factory_ = nullptr;
  filament::Material *fill_material_ = nullptr;
  std::size_t max_instances_per_chunk_ = 1u;
  CollisionShapeOverlayPlanner planner_;
  std::vector<MeshRenderableProxy> proxies_;
  InstancedWireRenderBackend wire_backend_;
  std::vector<WireRasterGroup> wire_groups_;
  float wire_near_plane_ = 0.05f;
  CollisionShapeOverlayCacheStats stats_;
};

} // namespace quader

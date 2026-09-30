////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/scene_render_extraction.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {

class ViewportSettings;

/**
 * Caches Mesh Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class MeshRenderableCache {
public:
    using MeshProxy = MeshRenderableProxy;

    /// Counts the cache update paths used by render-performance regressions.
    struct UpdateStats final {
      std::uint64_t topology_stable_geometry_rebind_count = 0u;
      std::uint64_t full_proxy_rebuild_count = 0u;
      std::size_t retired_proxy_count = 0u;
      std::size_t retired_proxy_high_watermark = 0u;
      std::size_t retired_material_instance_count = 0u;
      std::uint64_t destroyed_retired_proxy_count = 0u;
    };

    MeshRenderableCache() = default;
    MeshRenderableCache(const MeshRenderableCache&) = delete;
    MeshRenderableCache& operator=(const MeshRenderableCache&) = delete;
    MeshRenderableCache(MeshRenderableCache&&) = delete;
    MeshRenderableCache& operator=(MeshRenderableCache&&) = delete;
    ~MeshRenderableCache();

    void initialize(MeshRenderableFactory& factory);
    void shutdown();

    bool initialized() const {
        return factory_ != nullptr && factory_->initialized();
    }

    bool contains(EntityId entity_id) const;
    bool needs_rebuild(const RenderSceneMeshRecord &mesh) const;

    void sync_mesh(const RenderSceneMeshRecord &mesh);
    void update_surface_grid(float grid_world_spacing,
                             const ViewportSettings &settings);
    void set_preview_suppressed_entities(
        std::unordered_set<EntityId> entities);
    void remove(EntityId entity_id);
    void remove_except(const std::unordered_set<EntityId> &live_mesh_entities);
    // The elapsed budget is checked between complete proxies; one proxy may
    // exceed it. Unlimited drains are reserved for explicit lifecycle teardown.
    void drain_retired_destroy_queue(
        std::size_t max_proxy_count,
        std::chrono::nanoseconds max_duration = std::chrono::nanoseconds::max());

    std::size_t retired_proxy_count() const {
      return retired_mesh_proxies_.size();
    }

    [[nodiscard]] UpdateStats update_stats() const {
      return {
          .topology_stable_geometry_rebind_count =
              topology_stable_geometry_rebind_count_,
          .full_proxy_rebuild_count = full_proxy_rebuild_count_,
          .retired_proxy_count = retired_mesh_proxies_.size(),
          .retired_proxy_high_watermark = retired_proxy_high_watermark_,
          .retired_material_instance_count = retired_material_instance_count_,
          .destroyed_retired_proxy_count = destroyed_retired_proxy_count_,
      };
    }

    static std::uint64_t mesh_asset_signature(std::uint64_t hash,
                                              const MeshAsset &mesh);

  private:
    bool try_rebind_topology_stable_geometry(
        const RenderSceneMeshRecord &mesh);
    void retire_proxy(MeshRenderableProxy &&proxy);
    void apply_preview_suppression(EntityId entity_id);

    MeshRenderableFactory* factory_ = nullptr;
    std::unordered_map<EntityId, MeshRenderableProxy> rendered_mesh_proxies_;
    std::unordered_map<EntityId, MeshRenderableProxy> shaded_mesh_proxies_;
    std::unordered_map<EntityId, MeshRenderableProxy>
        overlay_depth_mesh_proxies_;
    std::vector<MeshRenderableProxy> retired_mesh_proxies_;
    std::unordered_set<EntityId> preview_suppressed_entities_;
    std::uint64_t surface_grid_signature_ = 0u;
    std::uint64_t topology_stable_geometry_rebind_count_ = 0u;
    std::uint64_t full_proxy_rebuild_count_ = 0u;
    std::size_t retired_proxy_high_watermark_ = 0u;
    std::size_t retired_material_instance_count_ = 0u;
    std::uint64_t destroyed_retired_proxy_count_ = 0u;
    bool surface_grid_dirty_ = true;
};

} // namespace quader

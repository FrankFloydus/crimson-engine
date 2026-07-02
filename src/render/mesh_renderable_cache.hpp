////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "scene/scene_document.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {

/**
 * Caches Mesh Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class MeshRenderableCache {
public:
    using MeshProxy = MeshRenderableProxy;

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
    bool needs_rebuild(EntityId entity_id, AssetId mesh_id,
                       const MeshAsset &mesh, AssetId material_id,
                       bool cast_shadows, bool double_sided,
                       std::uint64_t material_revision) const;

    void sync_mesh(EntityId entity_id, AssetId mesh_id, const MeshAsset &mesh,
                   AssetId material_id, const MaterialComponent *material,
                   const AssetRegistry &assets, const QMat4 &world_transform,
                   bool cast_shadows, bool double_sided,
                   std::uint64_t material_revision);
    void remove(EntityId entity_id);
    void remove_except(const std::unordered_set<EntityId> &live_mesh_entities);
    void drain_retired_destroy_queue(std::size_t max_proxy_count);

    std::size_t retired_proxy_count() const {
      return retired_mesh_proxies_.size();
    }

    static std::uint64_t mesh_asset_signature(std::uint64_t hash,
                                              const MeshAsset &mesh);

  private:
    void retire_proxy(MeshRenderableProxy &&proxy);

    MeshRenderableFactory* factory_ = nullptr;
    std::unordered_map<EntityId, MeshRenderableProxy> rendered_mesh_proxies_;
    std::unordered_map<EntityId, MeshRenderableProxy> shaded_mesh_proxies_;
    std::unordered_map<EntityId, MeshRenderableProxy>
        overlay_depth_mesh_proxies_;
    std::vector<MeshRenderableProxy> retired_mesh_proxies_;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/render_layers.hpp"

#include <utility>

namespace quader {
namespace {

bool mesh_proxy_needs_rebuild(
    const std::unordered_map<EntityId, MeshRenderableProxy> &proxies,
    EntityId entity_id, AssetId mesh_id, const MeshAsset &mesh,
    AssetId material_id, bool cast_shadows, bool double_sided,
    std::uint64_t material_revision) {
  const auto existing = proxies.find(entity_id);
  return existing == proxies.end() || existing->second.mesh != mesh_id ||
         existing->second.mesh_revision != mesh.revision.value ||
         existing->second.material != material_id ||
         existing->second.cast_shadows != cast_shadows ||
         existing->second.double_sided != double_sided ||
         existing->second.material_revision != material_revision;
}

void destroy_proxy_map(
    MeshRenderableFactory &factory,
    std::unordered_map<EntityId, MeshRenderableProxy> &proxies) {
  for (auto &[id, proxy] : proxies) {
    factory.destroy(proxy);
  }
  proxies.clear();
}

void destroy_detached_proxy_vector(MeshRenderableFactory &factory,
                                   std::vector<MeshRenderableProxy> &proxies) {
  for (MeshRenderableProxy &proxy : proxies) {
    factory.destroy_detached(proxy);
  }
  proxies.clear();
}

} // namespace

MeshRenderableCache::~MeshRenderableCache() {
    shutdown();
}

void MeshRenderableCache::initialize(MeshRenderableFactory& factory) {
    factory_ = &factory;
}

void MeshRenderableCache::shutdown() {
    if (!factory_) {
        return;
    }

    destroy_proxy_map(*factory_, rendered_mesh_proxies_);
    destroy_proxy_map(*factory_, shaded_mesh_proxies_);
    destroy_proxy_map(*factory_, overlay_depth_mesh_proxies_);
    destroy_detached_proxy_vector(*factory_, retired_mesh_proxies_);
    factory_ = nullptr;
}

bool MeshRenderableCache::contains(EntityId entity_id) const {
  return rendered_mesh_proxies_.contains(entity_id) ||
         shaded_mesh_proxies_.contains(entity_id) ||
         overlay_depth_mesh_proxies_.contains(entity_id);
}

bool MeshRenderableCache::needs_rebuild(EntityId entity_id, AssetId mesh_id,
                                        const MeshAsset &mesh,
                                        AssetId material_id, bool cast_shadows,
                                        bool double_sided,
                                        std::uint64_t material_revision) const {
  return mesh_proxy_needs_rebuild(rendered_mesh_proxies_, entity_id, mesh_id,
                                  mesh, material_id, cast_shadows, double_sided,
                                  material_revision) ||
         mesh_proxy_needs_rebuild(shaded_mesh_proxies_, entity_id, mesh_id,
                                  mesh, material_id, false, double_sided,
                                  material_revision) ||
         mesh_proxy_needs_rebuild(overlay_depth_mesh_proxies_, entity_id,
                                  mesh_id, mesh, material_id, false,
                                  double_sided, material_revision);
}

void MeshRenderableCache::sync_mesh(EntityId entity_id, AssetId mesh_id,
                                    const MeshAsset &mesh, AssetId material_id,
                                    const MaterialComponent *material,
                                    const AssetRegistry &assets,
                                    const QMat4 &world_transform,
                                    bool cast_shadows, bool double_sided,
                                    std::uint64_t material_revision) {
  QDR_PROFILE_FUNCTION();
  if (!factory_) {
    return;
  }

  if (needs_rebuild(entity_id, mesh_id, mesh, material_id, cast_shadows,
                    double_sided, material_revision)) {
    QDR_PROFILE_SCOPE("Mesh renderable rebuild proxies");
    remove(entity_id);
    MeshRenderableProxy rendered_proxy = factory_->create(
        entity_id, mesh_id, mesh, material, assets, world_transform,
        cast_shadows, double_sided, material_revision,
        kViewportRenderedSceneLayerMask, false);
    rendered_mesh_proxies_.emplace(entity_id, std::move(rendered_proxy));
    MeshRenderableProxy shaded_proxy = factory_->create(
        entity_id, mesh_id, mesh, material, assets, world_transform, false,
        double_sided, material_revision, kViewportShadedSceneLayerMask, true);
    shaded_mesh_proxies_.emplace(entity_id, std::move(shaded_proxy));
    MeshRenderableProxy overlay_depth_proxy = factory_->create(
        entity_id, mesh_id, mesh, material, assets, world_transform, false,
        double_sided, material_revision, kViewportOverlayLayerMask, true, true,
        kViewportOverlayDepthChannel);
    overlay_depth_mesh_proxies_.emplace(entity_id,
                                        std::move(overlay_depth_proxy));
    return;
  }

  if (auto existing = rendered_mesh_proxies_.find(entity_id);
      existing != rendered_mesh_proxies_.end()) {
    QDR_PROFILE_SCOPE("Mesh renderable update rendered transform");
    factory_->update_transform(existing->second, world_transform);
  }
  if (auto existing = shaded_mesh_proxies_.find(entity_id);
      existing != shaded_mesh_proxies_.end()) {
    QDR_PROFILE_SCOPE("Mesh renderable update shaded transform");
    factory_->update_transform(existing->second, world_transform);
  }
  if (auto existing = overlay_depth_mesh_proxies_.find(entity_id);
      existing != overlay_depth_mesh_proxies_.end()) {
    QDR_PROFILE_SCOPE("Mesh renderable update depth transform");
    factory_->update_transform(existing->second, world_transform);
  }
}

void MeshRenderableCache::remove(EntityId entity_id) {
  QDR_PROFILE_FUNCTION();
  if (!factory_) {
    return;
  }

  if (auto existing = rendered_mesh_proxies_.find(entity_id);
      existing != rendered_mesh_proxies_.end()) {
    retire_proxy(std::move(existing->second));
    rendered_mesh_proxies_.erase(existing);
  }
  if (auto existing = shaded_mesh_proxies_.find(entity_id);
      existing != shaded_mesh_proxies_.end()) {
    retire_proxy(std::move(existing->second));
    shaded_mesh_proxies_.erase(existing);
  }
  if (auto existing = overlay_depth_mesh_proxies_.find(entity_id);
      existing != overlay_depth_mesh_proxies_.end()) {
    retire_proxy(std::move(existing->second));
    overlay_depth_mesh_proxies_.erase(existing);
  }
}

void MeshRenderableCache::remove_except(
    const std::unordered_set<EntityId> &live_mesh_entities) {
  QDR_PROFILE_FUNCTION();
  if (!factory_) {
    return;
  }

  for (auto it = rendered_mesh_proxies_.begin();
       it != rendered_mesh_proxies_.end();) {
    if (!live_mesh_entities.contains(it->first)) {
      retire_proxy(std::move(it->second));
      it = rendered_mesh_proxies_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = shaded_mesh_proxies_.begin();
       it != shaded_mesh_proxies_.end();) {
    if (!live_mesh_entities.contains(it->first)) {
      retire_proxy(std::move(it->second));
      it = shaded_mesh_proxies_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = overlay_depth_mesh_proxies_.begin();
       it != overlay_depth_mesh_proxies_.end();) {
    if (!live_mesh_entities.contains(it->first)) {
      retire_proxy(std::move(it->second));
      it = overlay_depth_mesh_proxies_.erase(it);
    } else {
      ++it;
    }
  }
}

void MeshRenderableCache::drain_retired_destroy_queue(
    std::size_t max_proxy_count) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || max_proxy_count == 0u) {
    return;
  }

  for (std::size_t destroyed = 0u;
       destroyed < max_proxy_count && !retired_mesh_proxies_.empty();
       ++destroyed) {
    QDR_PROFILE_SCOPE("Mesh renderable destroy retired proxy");
    MeshRenderableProxy &proxy = retired_mesh_proxies_.back();
    factory_->destroy_detached(proxy);
    retired_mesh_proxies_.pop_back();
  }
}

std::uint64_t MeshRenderableCache::mesh_asset_signature(std::uint64_t hash,
                                                        const MeshAsset &mesh) {
  return MeshRenderableFactory::mesh_asset_signature(hash, mesh);
}

void MeshRenderableCache::retire_proxy(MeshRenderableProxy &&proxy) {
  if (!factory_ || !proxy.entity) {
    return;
  }

  QDR_PROFILE_FUNCTION();
  factory_->remove_from_scene(proxy);
  retired_mesh_proxies_.push_back(std::move(proxy));
}

} // namespace quader

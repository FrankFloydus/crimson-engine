////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/render_layers.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace quader {
namespace {

std::size_t owned_material_instance_count(const MeshRenderableProxy &proxy) {
  if (!proxy.owns_material_instances) {
    return 0u;
  }
  return static_cast<std::size_t>(std::count_if(
      proxy.material_instances.begin(), proxy.material_instances.end(),
      [](const auto *material) { return material != nullptr; }));
}

bool mesh_proxy_needs_rebuild(
    const std::unordered_map<EntityId, MeshRenderableProxy> &proxies,
    const RenderSceneMeshRecord &mesh, bool cast_shadows) {
  const auto existing = proxies.find(mesh.entity_id);
  return existing == proxies.end() ||
         existing->second.mesh != mesh.mesh.mesh_id ||
         existing->second.mesh_revision != mesh.mesh.mesh_revision ||
         existing->second.material != mesh.material_reference ||
         existing->second.cast_shadows != cast_shadows ||
         existing->second.double_sided != mesh.double_sided() ||
         existing->second.material_revision != mesh.material_revision;
}

bool mesh_proxy_allows_geometry_rebind(
    const std::unordered_map<EntityId, MeshRenderableProxy> &proxies,
    const RenderSceneMeshRecord &mesh, bool cast_shadows) {
  const auto existing = proxies.find(mesh.entity_id);
  return existing != proxies.end() &&
         existing->second.mesh == mesh.mesh.mesh_id &&
         existing->second.material == mesh.material_reference &&
         existing->second.cast_shadows == cast_shadows &&
         existing->second.double_sided == mesh.double_sided() &&
         existing->second.material_revision == mesh.material_revision;
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

void update_surface_grid_proxy_map(
    MeshRenderableFactory &factory,
    const std::unordered_map<EntityId, MeshRenderableProxy> &proxies,
    float grid_world_spacing, const ViewportSettings &settings) {
  for (const auto &entry : proxies) {
    factory.update_surface_grid(entry.second, grid_world_spacing, settings);
  }
}

void update_shaded_lighting_proxy_map(
    MeshRenderableFactory &factory,
    const std::unordered_map<EntityId, MeshRenderableProxy> &proxies,
    const ViewportSettings &settings) {
  for (const auto &entry : proxies) {
    factory.update_shaded_lighting(entry.second, settings);
  }
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

    // Filament validates every material release against all live renderables.
    // Retire the entire generation first so a large scene does not trigger a
    // quadratic scan while its three per-mesh proxies are being destroyed.
    std::vector<utils::Entity> entities;
    entities.reserve(rendered_mesh_proxies_.size() +
                     shaded_mesh_proxies_.size() +
                     overlay_depth_mesh_proxies_.size() +
                     retired_mesh_proxies_.size());
    const auto append_entities = [&entities](const auto &proxies) {
      for (const auto &[id, proxy] : proxies) {
        (void)id;
        if (proxy.entity) {
          entities.push_back(proxy.entity);
        }
      }
    };
    append_entities(rendered_mesh_proxies_);
    append_entities(shaded_mesh_proxies_);
    append_entities(overlay_depth_mesh_proxies_);
    for (const MeshRenderableProxy &proxy : retired_mesh_proxies_) {
      if (proxy.entity) {
        entities.push_back(proxy.entity);
      }
    }
    factory_->destroy_renderable_components(entities);
    destroy_proxy_map(*factory_, rendered_mesh_proxies_);
    destroy_proxy_map(*factory_, shaded_mesh_proxies_);
    destroy_proxy_map(*factory_, overlay_depth_mesh_proxies_);
    destroy_detached_proxy_vector(*factory_, retired_mesh_proxies_);
    surface_grid_signature_ = 0u;
    topology_stable_geometry_rebind_count_ = 0u;
    full_proxy_rebuild_count_ = 0u;
    retired_proxy_high_watermark_ = 0u;
    retired_material_instance_count_ = 0u;
    destroyed_retired_proxy_count_ = 0u;
    surface_grid_dirty_ = true;
    preview_suppressed_entities_.clear();
    factory_ = nullptr;
}

bool MeshRenderableCache::contains(EntityId entity_id) const {
  return rendered_mesh_proxies_.contains(entity_id) ||
         shaded_mesh_proxies_.contains(entity_id) ||
         overlay_depth_mesh_proxies_.contains(entity_id);
}

bool MeshRenderableCache::needs_rebuild(
    const RenderSceneMeshRecord &mesh) const {
  return mesh_proxy_needs_rebuild(rendered_mesh_proxies_, mesh,
                                  mesh.cast_shadows()) ||
         mesh_proxy_needs_rebuild(shaded_mesh_proxies_, mesh, false) ||
         mesh_proxy_needs_rebuild(overlay_depth_mesh_proxies_, mesh, false);
}

void MeshRenderableCache::sync_mesh(const RenderSceneMeshRecord &mesh) {
  QDR_PROFILE_FUNCTION();
  if (!factory_) {
    return;
  }

  if (needs_rebuild(mesh)) {
    if (try_rebind_topology_stable_geometry(mesh)) {
      QDR_PROFILE_SCOPE("Mesh renderable topology-stable rebind success");
      ++topology_stable_geometry_rebind_count_;
      if (retired_mesh_proxies_.empty()) {
        QDR_PROFILE_SCOPE("Mesh rebind retired queue remains empty");
      }
      return;
    }

    QDR_PROFILE_SCOPE("Mesh renderable rebuild proxies");
    ++full_proxy_rebuild_count_;
    remove(mesh.entity_id);
    MeshRenderableProxy rendered_proxy = factory_->create(
        mesh.entity_id, mesh.mesh, &mesh.material, mesh.world_transform,
        mesh.cast_shadows(), mesh.double_sided(), kViewportRenderedSceneLayerMask,
        false);
    factory_->update_texture_mapping(rendered_proxy, mesh.sampling_from_local);
    rendered_mesh_proxies_.emplace(mesh.entity_id, std::move(rendered_proxy));
    MeshRenderableProxy shaded_proxy = factory_->create(
        mesh.entity_id, mesh.mesh, &mesh.material, mesh.world_transform, false,
        mesh.double_sided(), kViewportShadedSceneLayerMask, true);
    factory_->update_texture_mapping(shaded_proxy, mesh.sampling_from_local);
    shaded_mesh_proxies_.emplace(mesh.entity_id, std::move(shaded_proxy));
    MeshRenderableProxy overlay_depth_proxy = factory_->create(
        mesh.entity_id, mesh.mesh, &mesh.material, mesh.world_transform, false,
        mesh.double_sided(), kViewportOverlayLayerMask, true, true,
        kViewportOverlayDepthChannel);
    factory_->update_texture_mapping(overlay_depth_proxy, mesh.sampling_from_local);
    overlay_depth_mesh_proxies_.emplace(mesh.entity_id,
                                        std::move(overlay_depth_proxy));
    apply_preview_suppression(mesh.entity_id);
    surface_grid_dirty_ = true;
    return;
  }

  if (auto existing = rendered_mesh_proxies_.find(mesh.entity_id);
      existing != rendered_mesh_proxies_.end()) {
    QDR_PROFILE_SCOPE("Mesh renderable update rendered transform");
    if (mesh.transform_dirty) {
      factory_->update_transform(existing->second, mesh.world_transform);
    }
    if (mesh.texture_mapping_dirty) {
      factory_->update_texture_mapping(existing->second, mesh.sampling_from_local);
    }
  }
  if (auto existing = shaded_mesh_proxies_.find(mesh.entity_id);
      existing != shaded_mesh_proxies_.end()) {
    QDR_PROFILE_SCOPE("Mesh renderable update shaded transform");
    if (mesh.transform_dirty) {
      factory_->update_transform(existing->second, mesh.world_transform);
    }
    if (mesh.texture_mapping_dirty) {
      factory_->update_texture_mapping(existing->second, mesh.sampling_from_local);
    }
  }
  if (auto existing = overlay_depth_mesh_proxies_.find(mesh.entity_id);
      existing != overlay_depth_mesh_proxies_.end()) {
    QDR_PROFILE_SCOPE("Mesh renderable update depth transform");
    if (mesh.transform_dirty) {
      factory_->update_transform(existing->second, mesh.world_transform);
    }
    if (mesh.texture_mapping_dirty) {
      factory_->update_texture_mapping(existing->second, mesh.sampling_from_local);
    }
  }
}

bool MeshRenderableCache::try_rebind_topology_stable_geometry(
    const RenderSceneMeshRecord &mesh) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ ||
      !mesh_proxy_allows_geometry_rebind(rendered_mesh_proxies_, mesh,
                                         mesh.cast_shadows()) ||
      !mesh_proxy_allows_geometry_rebind(shaded_mesh_proxies_, mesh, false) ||
      !mesh_proxy_allows_geometry_rebind(overlay_depth_mesh_proxies_, mesh,
                                         false)) {
    return false;
  }

  MeshRenderableProxy &rendered =
      rendered_mesh_proxies_.find(mesh.entity_id)->second;
  MeshRenderableProxy &shaded =
      shaded_mesh_proxies_.find(mesh.entity_id)->second;
  MeshRenderableProxy &overlay_depth =
      overlay_depth_mesh_proxies_.find(mesh.entity_id)->second;
  std::array<MeshRenderableProxy *, 3u> proxies{
      &rendered,
      &shaded,
      &overlay_depth,
  };
  if (!factory_->try_rebind_static_geometry(proxies, mesh.mesh)) {
    return false;
  }

  factory_->update_transform(rendered, mesh.world_transform);
  factory_->update_transform(shaded, mesh.world_transform);
  factory_->update_transform(overlay_depth, mesh.world_transform);
  factory_->update_texture_mapping(rendered, mesh.sampling_from_local);
  factory_->update_texture_mapping(shaded, mesh.sampling_from_local);
  factory_->update_texture_mapping(overlay_depth, mesh.sampling_from_local);
  apply_preview_suppression(mesh.entity_id);
  return true;
}

void MeshRenderableCache::set_preview_suppressed_entities(
    std::unordered_set<EntityId> entities) {
  if (preview_suppressed_entities_ == entities) {
    return;
  }
  std::unordered_set<EntityId> affected = preview_suppressed_entities_;
  affected.insert(entities.begin(), entities.end());
  preview_suppressed_entities_ = std::move(entities);
  for (const EntityId entity : affected) {
    apply_preview_suppression(entity);
  }
}

void MeshRenderableCache::apply_preview_suppression(EntityId entity_id) {
  if (!factory_) {
    return;
  }
  const bool suppressed = preview_suppressed_entities_.contains(entity_id);
  if (const auto proxy = rendered_mesh_proxies_.find(entity_id);
      proxy != rendered_mesh_proxies_.end()) {
    factory_->update_layer_mask(
        proxy->second,
        suppressed ? 0u : kViewportRenderedSceneLayerMask);
  }
  if (const auto proxy = shaded_mesh_proxies_.find(entity_id);
      proxy != shaded_mesh_proxies_.end()) {
    factory_->update_layer_mask(
        proxy->second, suppressed ? 0u : kViewportShadedSceneLayerMask);
  }
  if (const auto proxy = overlay_depth_mesh_proxies_.find(entity_id);
      proxy != overlay_depth_mesh_proxies_.end()) {
    factory_->update_layer_mask(
        proxy->second, suppressed ? 0u : kViewportOverlayLayerMask);
  }
}

void MeshRenderableCache::update_surface_grid(
    float grid_world_spacing, const ViewportSettings &settings) {
  QDR_PROFILE_FUNCTION();
  if (!factory_) {
    return;
  }

  const std::uint64_t signature =
      mesh_surface_grid_signature(grid_world_spacing, settings);
  if (!surface_grid_dirty_ && surface_grid_signature_ == signature) {
    return;
  }

  update_surface_grid_proxy_map(*factory_, rendered_mesh_proxies_,
                                grid_world_spacing, settings);
  update_surface_grid_proxy_map(*factory_, shaded_mesh_proxies_,
                                grid_world_spacing, settings);
  update_shaded_lighting_proxy_map(*factory_, shaded_mesh_proxies_, settings);
  surface_grid_signature_ = signature;
  surface_grid_dirty_ = false;
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
    std::size_t max_proxy_count, std::chrono::nanoseconds max_duration) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || max_proxy_count == 0u || retired_mesh_proxies_.empty() ||
      max_duration <= std::chrono::nanoseconds::zero()) {
    return;
  }

  const bool time_limited = max_duration != std::chrono::nanoseconds::max();
  const auto started = time_limited ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
  for (std::size_t destroyed = 0u;
       destroyed < max_proxy_count && !retired_mesh_proxies_.empty();
       ++destroyed) {
    QDR_PROFILE_SCOPE("Mesh renderable destroy retired proxy");
    MeshRenderableProxy &proxy = retired_mesh_proxies_.back();
    retired_material_instance_count_ -= owned_material_instance_count(proxy);
    factory_->destroy_detached(proxy);
    retired_mesh_proxies_.pop_back();
    ++destroyed_retired_proxy_count_;
    if (time_limited && std::chrono::steady_clock::now() - started >= max_duration) {
      break;
    }
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
  // Remove the renderable component before releasing any retired material.
  // Scene removal alone leaves it in Filament's material-lifetime validation
  // scans. The factory submits bounded command batches during this detach.
  factory_->destroy_renderable_components(std::span(&proxy.entity, 1u));
  retired_material_instance_count_ += owned_material_instance_count(proxy);
  retired_mesh_proxies_.push_back(std::move(proxy));
  retired_proxy_high_watermark_ =
      std::max(retired_proxy_high_watermark_, retired_mesh_proxies_.size());
}

} // namespace quader

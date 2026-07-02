////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_sync_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/scene_render_extraction.hpp"

namespace quader {
namespace {

constexpr std::size_t kRetiredMeshProxyDestroyBudgetPerSync = 1u;

} // namespace

SceneRenderSyncService::~SceneRenderSyncService() {
    shutdown();
}

void SceneRenderSyncService::initialize(
    filament::Engine &engine, filament::Scene &scene,
    MeshRenderableCache &mesh_cache, MaterialBindingService &material_binding) {
  engine_ = &engine;
  scene_ = &scene;
  mesh_cache_ = &mesh_cache;
  material_binding_ = &material_binding;
  light_cache_.initialize(engine, scene);
}

void SceneRenderSyncService::shutdown() {
    if (!engine_) {
        return;
    }

    light_cache_.shutdown();
    synced_revision_ = 0;
    synced_asset_revision_ = 0;
    has_synced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    mesh_cache_ = nullptr;
    material_binding_ = nullptr;
}

bool SceneRenderSyncService::sync(const SceneDocument& document, const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return false;
    }
    {
        QDR_PROFILE_SCOPE("Scene render drain retired mesh proxies");
        mesh_cache_->drain_retired_destroy_queue(
            kRetiredMeshProxyDestroyBudgetPerSync);
    }

    const bool document_unchanged =
        has_synced_ && synced_revision_ == document.revision();
    const bool assets_unchanged =
        has_synced_ && synced_asset_revision_ == assets.revision();
    if (document_unchanged && assets_unchanged) {
      return false;
    }
    if (document_unchanged &&
        !render_asset_revisions_changed(document, assets)) {
      synced_asset_revision_ = assets.revision();
      return false;
    }

    {
        QDR_PROFILE_SCOPE("Scene render rebuild");
        rebuild(document, assets);
    }
    synced_revision_ = document.revision();
    synced_asset_revision_ = assets.revision();
    has_synced_ = true;
    return true;
}

bool SceneRenderSyncService::render_asset_revisions_changed(
    const SceneDocument &document, const AssetRegistry &assets) const {
  QDR_PROFILE_FUNCTION();
  const SceneRenderExtractionPlan plan = [&]() {
    QDR_PROFILE_SCOPE("Scene render revision extraction plan");
    return build_scene_render_extraction_plan(document, assets,
                                              *material_binding_);
  }();
  for (const SceneRenderMeshEntry &mesh_entry : plan.meshes) {
    if (!mesh_entry.renderable()) {
      if (mesh_cache_->contains(mesh_entry.entity_id)) {
        return true;
      }
      continue;
    }

    if (mesh_cache_->needs_rebuild(
            mesh_entry.entity_id, mesh_entry.mesh_id, *mesh_entry.mesh,
            mesh_entry.material_id, mesh_entry.cast_shadows(),
            mesh_entry.double_sided(), mesh_entry.material_revision)) {
      return true;
    }
  }
  return false;
}

void SceneRenderSyncService::rebuild(const SceneDocument& document, const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    const SceneRenderExtractionPlan plan = [&]() {
        QDR_PROFILE_SCOPE("Scene render extraction plan");
        return build_scene_render_extraction_plan(document, assets,
                                                  *material_binding_);
    }();

    for (const SceneRenderMeshEntry &mesh_entry : plan.meshes) {
      QDR_PROFILE_SCOPE("Scene render sync mesh entry");
      if (!mesh_entry.renderable()) {
        mesh_cache_->remove(mesh_entry.entity_id);
        LOG_WARN("Skipping render entity {} because its mesh asset is missing "
                 "or empty",
                 mesh_entry.entity_id);
        continue;
      }

      mesh_cache_->sync_mesh(
          mesh_entry.entity_id, mesh_entry.mesh_id, *mesh_entry.mesh,
          mesh_entry.material_id, mesh_entry.material_component, assets,
          mesh_entry.world_transform, mesh_entry.cast_shadows(),
          mesh_entry.double_sided(), mesh_entry.material_revision);
    }

    for (const SceneRenderLightEntry &light_entry : plan.lights) {
      QDR_PROFILE_SCOPE("Scene render sync light entry");
      light_cache_.sync_light(light_entry.entity_id, *light_entry.light,
                              light_entry.world_transform);
    }

    {
        QDR_PROFILE_SCOPE("Scene render remove stale meshes");
        mesh_cache_->remove_except(plan.live_mesh_entities);
    }
    {
        QDR_PROFILE_SCOPE("Scene render remove stale lights");
        light_cache_.remove_except(plan.live_light_entities);
    }
}

} // namespace quader

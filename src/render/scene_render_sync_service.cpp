////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_sync_service.hpp"

#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/instance_renderable_cache.hpp"
#include "render/mesh_renderable_cache.hpp"

#include <filament/TransformManager.h>

#include <unordered_set>

namespace quader {
namespace {

constexpr std::size_t kRetiredMeshProxyDestroyBudgetPerSync = 1u;

/**
 * Commits a Filament local-transform transaction on every exit path.
 */
class LocalTransformTransaction final {
public:
  explicit LocalTransformTransaction(filament::TransformManager &transforms)
      : transforms_(transforms) {
    transforms_.openLocalTransformTransaction();
  }

  LocalTransformTransaction(const LocalTransformTransaction &) = delete;
  LocalTransformTransaction &
  operator=(const LocalTransformTransaction &) = delete;

  ~LocalTransformTransaction() {
    transforms_.commitLocalTransformTransaction();
  }

private:
  filament::TransformManager &transforms_;
};

} // namespace

SceneRenderSyncService::~SceneRenderSyncService() {
    shutdown();
}

void SceneRenderSyncService::initialize(
    filament::Engine &engine, filament::Scene &scene,
    MeshRenderableCache &mesh_cache, InstanceRenderableCache &instance_cache,
    MaterialBindingService &material_binding) {
  engine_ = &engine;
  scene_ = &scene;
  mesh_cache_ = &mesh_cache;
  instance_cache_ = &instance_cache;
  material_binding_ = &material_binding;
  light_cache_.initialize(engine, scene);
}

void SceneRenderSyncService::shutdown() {
    if (!engine_) {
        return;
    }

    light_cache_.shutdown();
    synced_source_stamp_ = {};
    has_scene_binding_ = false;
    has_synced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    mesh_cache_ = nullptr;
    instance_cache_ = nullptr;
    material_binding_ = nullptr;
}

void SceneRenderSyncService::bind_scene(
    RenderSceneBindingKey scene_binding) {
  if (!initialized() ||
      (has_scene_binding_ &&
       synced_source_stamp_.scene_binding == scene_binding)) {
    return;
  }

  const std::unordered_set<EntityId> no_scene_entities;
  mesh_cache_->remove_except(no_scene_entities);
  instance_cache_->clear();
  light_cache_.set_preview_suppressed_entities({});
  light_cache_.remove_except(no_scene_entities);
  // Material instances retain their bound texture pointers even after their
  // renderables leave the Filament scene. A scene switch invalidates the
  // scene-qualified texture cache, so every retired mesh proxy must release
  // its material instances before MaterialBindingService destroys those
  // textures. The normal paced retirement budget remains appropriate for
  // same-scene rebuilds, but cannot cross this ownership boundary.
  mesh_cache_->drain_retired_destroy_queue(
      mesh_cache_->retired_proxy_count());
  material_binding_->bind_scene(scene_binding);
  synced_source_stamp_ = {.scene_binding = scene_binding};
  has_scene_binding_ = true;
  has_synced_ = false;
}

bool SceneRenderSyncService::sync(const RenderSceneSnapshot &snapshot) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return false;
    }
    {
        QDR_PROFILE_SCOPE("Scene render drain retired mesh proxies");
        mesh_cache_->drain_retired_destroy_queue(
            kRetiredMeshProxyDestroyBudgetPerSync);
    }

    bind_scene(snapshot.scene_binding);

    const bool document_unchanged =
        has_synced_ && render_source_dependencies_match(
                           synced_source_stamp_, snapshot,
                           kRenderSceneSyncDocumentDependencies);
    const bool assets_unchanged =
        has_synced_ && render_source_dependencies_match(
                           synced_source_stamp_, snapshot,
                           kRenderSceneSyncAssetDependencies);
    if (document_unchanged && assets_unchanged &&
        instance_cache_->last_sync_succeeded()) {
      return false;
    }
    if (document_unchanged && !render_asset_revisions_changed(snapshot)) {
      synced_source_stamp_ = snapshot;
      return false;
    }
    if (has_synced_ &&
        render_source_dependencies_match(
            synced_source_stamp_, snapshot,
            kRenderSceneSyncContextDependencies) &&
        snapshot.reused_previous_records &&
        !snapshot.dirty.selection_changed &&
        !snapshot.dirty.visibility_changed &&
        !snapshot.dirty.local_visibility_changed &&
        !snapshot.dirty.transforms_changed &&
        !snapshot.dirty.texture_mappings_changed &&
        !snapshot.dirty.mesh_bindings_changed &&
        !snapshot.dirty.render_components_changed &&
        !snapshot.dirty.assets_changed &&
        !snapshot.dirty.project_materials_changed &&
        instance_cache_->last_sync_succeeded()) {
      // Extraction proved that only non-main-scene owner stamps advanced.
      // A curve-only publication reaches its independent overlay cache, while
      // the exact retained mesh/light records require no Filament work.
      synced_source_stamp_ = snapshot;
      return false;
    }

    {
        QDR_PROFILE_SCOPE("Scene render rebuild");
        rebuild(snapshot);
    }
    if (!instance_cache_->last_sync_succeeded()) {
      return false;
    }
    synced_source_stamp_ = snapshot;
    has_synced_ = true;
    return true;
}

bool SceneRenderSyncService::render_asset_revisions_changed(
    const RenderSceneSnapshot &snapshot) const {
  QDR_PROFILE_FUNCTION();
  if (!instance_cache_->last_sync_succeeded()) {
    return true;
  }
  for (const RenderSceneMeshRecord &mesh_entry : snapshot.meshes) {
    if (!mesh_entry.renderable()) {
      if (mesh_cache_->contains(mesh_entry.entity_id)) {
        return true;
      }
      continue;
    }
    if (mesh_cache_->needs_rebuild(mesh_entry)) {
      return true;
    }
  }
  if (instance_cache_->classify_update(snapshot.instance_primitives) ==
      RenderInstanceBatchUpdateKind::Rebuild) {
    return true;
  }
  return false;
}

void SceneRenderSyncService::set_preview_suppressed_lights(
    std::unordered_set<EntityId> entities,
    std::span<const RenderSceneLightRecord> retained_lights) {
  light_cache_.set_preview_suppressed_entities(std::move(entities),
                                               retained_lights);
}

bool SceneRenderSyncService::can_sync_transforms_only(
    const RenderSceneSnapshot &snapshot) const {
  if (!has_synced_ ||
      !render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          kRenderSceneSyncContextDependencies) ||
      (!snapshot.dirty.transforms_changed && !snapshot.dirty.texture_mappings_changed) ||
      snapshot.dirty.visibility_changed ||
      snapshot.dirty.local_visibility_changed ||
      snapshot.dirty.mesh_bindings_changed ||
      snapshot.dirty.render_components_changed ||
      snapshot.dirty.assets_changed ||
      snapshot.dirty.project_materials_changed) {
    return false;
  }

  for (const RenderSceneMeshRecord &mesh : snapshot.meshes) {
    if (mesh.mesh_dirty || mesh.material_dirty || mesh.binding_dirty) {
      return false;
    }
    if (mesh.renderable()) {
      if (mesh_cache_->needs_rebuild(mesh)) {
        return false;
      }
    } else if (mesh_cache_->contains(mesh.entity_id)) {
      return false;
    }
  }
  // InstanceRenderableCache retains semantic occurrence/chunk slots and owns
  // the validated transform-vs-rebuild decision. Avoid preparing the complete
  // instance plan here and then preparing it a second time in
  // sync_dirty_transforms().
  for (const RenderSceneLightRecord &light : snapshot.lights) {
    if (light.light_dirty || light_cache_.needs_rebuild(light)) {
      return false;
    }
  }
  return true;
}

void SceneRenderSyncService::sync_dirty_transforms(
    const RenderSceneSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  {
    filament::TransformManager &transforms = engine_->getTransformManager();
    LocalTransformTransaction transaction(transforms);
    for (const RenderSceneMeshRecord &mesh : snapshot.meshes) {
      if (mesh.renderable() && (mesh.transform_dirty || mesh.texture_mapping_dirty)) {
        QDR_PROFILE_SCOPE("Scene render sync dirty mesh transform");
        mesh_cache_->sync_mesh(mesh);
      }
    }
  }

  {
    QDR_PROFILE_SCOPE("Scene render update instance transforms");
    (void)instance_cache_->sync(snapshot.instance_primitives);
  }
  if (!instance_cache_->last_sync_succeeded()) {
    return;
  }

  for (const RenderSceneLightRecord &light : snapshot.lights) {
    if (light.transform_dirty) {
      QDR_PROFILE_SCOPE("Scene render sync dirty light transform");
      light_cache_.sync_light(light);
    }
  }
}

void SceneRenderSyncService::rebuild(const RenderSceneSnapshot &snapshot) {
    QDR_PROFILE_FUNCTION();
    if (can_sync_transforms_only(snapshot)) {
      QDR_PROFILE_SCOPE("Scene render transform-only sync");
      sync_dirty_transforms(snapshot);
      return;
    }

    for (const RenderSceneMeshRecord &mesh_entry : snapshot.meshes) {
      QDR_PROFILE_SCOPE("Scene render sync mesh entry");
      if (!mesh_entry.renderable()) {
        mesh_cache_->remove(mesh_entry.entity_id);
        LOG_WARN("Skipping render entity {} because its mesh asset is missing "
                 "or empty",
                 mesh_entry.entity_id);
        continue;
      }
      mesh_cache_->sync_mesh(mesh_entry);
    }

    {
      QDR_PROFILE_SCOPE("Scene render sync explicit instance batches");
      (void)instance_cache_->sync(snapshot.instance_primitives);
    }
    if (!instance_cache_->last_sync_succeeded()) {
      return;
    }

    for (const RenderSceneLightRecord &light_entry : snapshot.lights) {
      QDR_PROFILE_SCOPE("Scene render sync light entry");
      light_cache_.sync_light(light_entry);
    }

    {
        QDR_PROFILE_SCOPE("Scene render remove stale meshes");
        mesh_cache_->remove_except(snapshot.live_mesh_entities);
    }
    {
        QDR_PROFILE_SCOPE("Scene render remove stale lights");
        light_cache_.remove_except(snapshot.live_light_entities);
    }
}

} // namespace quader

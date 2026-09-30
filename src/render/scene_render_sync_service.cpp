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

#include <chrono>
#include <algorithm>
#include <unordered_set>

namespace quader {
namespace {

constexpr std::size_t kRetiredMeshProxyDestroyBudgetPerSync = 256u;
constexpr auto kRetiredMeshProxyDestroyTimePerSync = std::chrono::milliseconds(2);

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
    retained_mesh_exclusion_ = {};
    retained_mesh_entities_.clear();
    ordinary_snapshot_ = {};
    ordinary_snapshot_valid_ = false;
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
  // Finish the outgoing scene's retirement now. Each destroyed material
  // releases its texture pins, so images disappear when their final consumer
  // drains. Same-scene rebuilds keep the normal paced destruction budget.
  mesh_cache_->drain_retired_destroy_queue(
      mesh_cache_->retired_proxy_count());
  material_binding_->bind_scene(scene_binding);
  synced_source_stamp_ = {.scene_binding = scene_binding};
  has_scene_binding_ = true;
  has_synced_ = false;
  retained_mesh_exclusion_ = {};
  retained_mesh_entities_.clear();
  ordinary_snapshot_ = {};
  ordinary_snapshot_valid_ = false;
}

bool SceneRenderSyncService::set_retained_mesh_exclusion(
    const RenderToolPreviewSuppressionSnapshot &exclusion) {
  if (retained_mesh_exclusion_.signature == exclusion.signature &&
      retained_mesh_exclusion_.entities == exclusion.entities &&
      retained_mesh_exclusion_.occurrences == exclusion.occurrences) {
    return false;
  }
  std::unordered_set<EntityId> entities(exclusion.entities.begin(),
                                        exclusion.entities.end());
  retained_mesh_exclusion_ = exclusion;
  retained_mesh_entities_ = std::move(entities);
  ordinary_snapshot_valid_ = false;
  // Coverage is a render-owner change even when document revisions are fixed.
  has_synced_ = false;
  return true;
}

std::span<const RenderSceneInstancePrimitiveRecord>
SceneRenderSyncService::ordinary_instance_primitives(const RenderSceneSnapshot &snapshot) {
  return ordinary_snapshot(snapshot).instance_primitives;
}

const RenderSceneSnapshot &SceneRenderSyncService::ordinary_snapshot(
    const RenderSceneSnapshot &snapshot) {
  if (retained_mesh_exclusion_.empty()) {
    return snapshot;
  }
  if (ordinary_snapshot_valid_ &&
      ordinary_snapshot_.light_property_revision == snapshot.light_property_revision &&
      static_cast<const RenderSourceStamp &>(ordinary_snapshot_) ==
          static_cast<const RenderSourceStamp &>(snapshot)) {
    return ordinary_snapshot_;
  }
  QDR_PROFILE_SCOPE("Scene retain externally covered mesh records");
  RenderSceneSnapshot ordinary;
  static_cast<RenderSourceStamp &>(ordinary) = snapshot;
  ordinary.frame_index = snapshot.frame_index;
  ordinary.light_property_revision = snapshot.light_property_revision;
  ordinary.dirty = snapshot.dirty;
  ordinary.reused_previous_records = false;
  ordinary.has_solidified_curves = snapshot.has_solidified_curves;
  ordinary.lights = snapshot.lights;
  ordinary.live_light_entities = snapshot.live_light_entities;
  ordinary.material_binding_cache = snapshot.material_binding_cache;
  ordinary.extraction_work = snapshot.extraction_work;
  for (const auto &mesh : snapshot.meshes) {
    if (!retained_mesh_entities_.contains(mesh.entity_id)) {
      ordinary.meshes.push_back(mesh);
    }
  }
  for (const EntityId entity : snapshot.live_mesh_entities) {
    if (!retained_mesh_entities_.contains(entity)) {
      ordinary.live_mesh_entities.insert(entity);
    }
  }
  for (const auto &primitive : snapshot.instance_primitives) {
    const auto covered = [&](const RenderSceneInstanceOccurrenceRecord &occurrence) {
      if (retained_mesh_entities_.contains(occurrence.entity_id)) {
        return true;
      }
      return std::ranges::any_of(retained_mesh_exclusion_.occurrences,
          [&](const RenderInstancePreviewSuppression &suppression) {
            return suppression.occurrence == occurrence.entity_id &&
                   (suppression.source_entity == primitive.definition_entity ||
                    suppression.source_entity == primitive.member_entity);
          });
    };
    const auto remaining = std::ranges::count_if(primitive.occurrences,
        [&](const auto &occurrence) { return !covered(occurrence); });
    if (remaining == 0) {
      continue;
    }
    auto &retained = ordinary.instance_primitives.emplace_back();
    retained.definition_entity = primitive.definition_entity;
    retained.member_entity = primitive.member_entity;
    retained.member_from_definition = primitive.member_from_definition;
    retained.key = primitive.key;
    retained.mesh = primitive.mesh;
    retained.primitive = primitive.primitive;
    retained.material = primitive.material;
    retained.local_bounds = primitive.local_bounds;
    retained.occurrences.reserve(static_cast<std::size_t>(remaining));
    for (const auto &occurrence : primitive.occurrences) {
      if (!covered(occurrence)) {
        retained.occurrences.push_back(occurrence);
      }
    }
  }
  ordinary_snapshot_ = std::move(ordinary);
  ordinary_snapshot_valid_ = true;
  return ordinary_snapshot_;
}

bool SceneRenderSyncService::sync(const RenderSceneSnapshot &source_snapshot) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return false;
    }
    {
        QDR_PROFILE_SCOPE("Scene render drain retired mesh proxies");
        mesh_cache_->drain_retired_destroy_queue(
            kRetiredMeshProxyDestroyBudgetPerSync,
            kRetiredMeshProxyDestroyTimePerSync);
        const auto retirement_stats = mesh_cache_->update_stats();
        QDR_PROFILE_PLOT(
            "Quader ordinary retired proxies",
            static_cast<std::int64_t>(retirement_stats.retired_proxy_count));
        QDR_PROFILE_PLOT(
            "Quader ordinary retired material instances",
            static_cast<std::int64_t>(retirement_stats.retired_material_instance_count));
        QDR_PROFILE_PLOT(
            "Quader ordinary destroyed retired proxies",
            static_cast<std::int64_t>(retirement_stats.destroyed_retired_proxy_count));
        (void)retirement_stats;
    }

    bind_scene(source_snapshot.scene_binding);
    const RenderSceneSnapshot &snapshot = ordinary_snapshot(source_snapshot);

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

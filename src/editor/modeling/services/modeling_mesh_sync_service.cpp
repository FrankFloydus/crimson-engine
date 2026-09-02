////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/runtime/modeling_object_binding_registry.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "scene/scene_document.hpp"

#include <quader/modeling/curves/curve_document.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

[[nodiscard]] bool apply_complete_scene_identity_to_entity(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity, bool include_selection,
    bool include_hierarchy_placement) {
  if (!scene.exists(entity) ||
      !scene.rename_entity(entity, identity.name) ||
      (!identity.uuid.empty() && scene.uuid(entity) != identity.uuid &&
       !scene.set_entity_uuid(entity, identity.uuid)) ||
      (identity.editor_object_id != kInvalidEditorObjectId &&
       scene.editor_object_id(entity) != identity.editor_object_id &&
       !scene.set_editor_object_id(entity, identity.editor_object_id))) {
    return false;
  }
  {
    QDR_PROFILE_SCOPE("Modeling mesh sync apply complete scene identity");
    if (!apply_scene_entity_state(
            scene, entity, identity,
            {.include_selection = include_selection,
             .include_hierarchy_placement = false})) {
      return false;
    }
  }
  if (!include_hierarchy_placement) {
    return true;
  }
  const EntityId parent =
      identity.parent != kInvalidEntityId && scene.exists(identity.parent)
          ? identity.parent
          : scene.scene_root();
  if (parent == identity.parent) {
    return apply_scene_entity_state_placement(scene, entity, identity,
                                              parent);
  }
  return scene.parent(entity) == parent ||
         scene.set_parent(entity, parent, false);
}

} // namespace

bool ModelingMeshSyncService::should_force_refresh(bool preview_active,
                                                   bool preview_finished) {
  return preview_active || preview_finished;
}

std::optional<MeshAsset> ModelingMeshSyncService::compile_object_mesh(
    const quader_editor_core::PolygonObject &object) const {
  return MeshProjectionService::compile_object_mesh(object);
}

AssetId ModelingMeshSyncService::install_projected_mesh(
    AssetRegistry &assets, MeshAsset mesh) const {
  return assets.add_mesh(std::move(mesh));
}

bool ModelingMeshSyncService::replace_projected_mesh(
    AssetRegistry &assets, AssetId mesh_id, MeshAsset mesh) const {
  return assets.replace_mesh(mesh_id, std::move(mesh));
}

AssetId ModelingMeshSyncService::install_projected_curve(
    AssetRegistry &assets, CurveAsset curve) const {
  return assets.add_curve(std::move(curve));
}

bool ModelingMeshSyncService::replace_projected_curve(
    AssetRegistry &assets, AssetId curve_id, CurveAsset curve) const {
  return assets.replace_curve(curve_id, std::move(curve));
}

EntityId ModelingMeshSyncService::create_projected_mesh_entity(
    SceneDocument &scene, const std::string &name) const {
  return scene.create_entity(name, EntityKind::Mesh);
}

EntityId ModelingMeshSyncService::create_projected_curve_entity(
    SceneDocument &scene, const std::string &name) const {
  return scene.create_entity(name, EntityKind::Curve);
}

EntityId ModelingMeshSyncService::restore_projected_mesh_entity(
    SceneDocument &scene, const ModelingObjectSceneIdentity &identity) const {
  if (scene.exists(identity.id)) {
    return identity.id;
  }
  const EntityId restored = scene.create_entity_with_id(
      identity.id, identity.name, identity.kind, identity.editor_object_id);
  if (restored == kInvalidEntityId) {
    return kInvalidEntityId;
  }
  if (!identity.uuid.empty() &&
      !scene.set_entity_uuid(restored, identity.uuid)) {
    (void)scene.destroy_entity(restored);
    return kInvalidEntityId;
  }
  return restored;
}

bool ModelingMeshSyncService::destroy_projected_entity(
    SceneDocument &scene, EntityId entity) const {
  return scene.destroy_entity(entity);
}

bool ModelingMeshSyncService::parent_projected_entity(
    SceneDocument &scene, EntityId entity, EntityId parent) const {
  return scene.set_parent(entity, parent, false);
}

bool ModelingMeshSyncService::set_projected_transform(
    SceneDocument &scene, EntityId entity,
    const TransformComponent &transform) const {
  return scene.set_transform(entity, transform);
}

bool ModelingMeshSyncService::set_projected_texture_mapping(
    SceneDocument &scene, EntityId entity,
    const TextureMappingComponent &mapping) const {
  // An absent mapping already means identity. Do not publish a redundant
  // component/revision for generated geometry created in the identity frame.
  if (scene.exists(entity) && scene.kind(entity) == EntityKind::Mesh &&
      scene.texture_mapping(entity) == nullptr &&
      mapping.sampling_from_local.values == QMat4{}.values) {
    return true;
  }
  return scene.set_texture_mapping(entity, mapping);
}

bool ModelingMeshSyncService::set_projected_mesh_component(
    SceneDocument &scene, EntityId entity, const MeshComponent &mesh) const {
  return scene.set_mesh(entity, mesh);
}

bool ModelingMeshSyncService::set_projected_modeling_object_component(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectComponent &modeling_object) const {
  return scene.set_modeling_object(entity, modeling_object);
}

bool ModelingMeshSyncService::bind_projected_object(
    SceneDocument &scene, EntityId entity, int modeling_object_id,
    std::uint32_t material_slot, bool sdk_owned, AssetId mesh_id,
    std::optional<std::uint32_t> requested_local_frame_version) const {
  const ModelingObjectComponent *current_modeling_object =
      scene.modeling_object(entity);
  std::uint32_t local_frame_version =
      kLegacyUnknownModelingObjectLocalFrameVersion;
  if (requested_local_frame_version.has_value()) {
    local_frame_version = *requested_local_frame_version;
  } else if (current_modeling_object != nullptr) {
    local_frame_version = current_modeling_object->local_frame_version;
  } else if (sdk_owned) {
    local_frame_version = kCanonicalModelingObjectLocalFrameVersion;
  }
  MeshComponent mesh = scene.mesh(entity) != nullptr
                           ? *scene.mesh(entity)
                           : MeshComponent{};
  mesh.mesh = mesh_id;
  return scene.set_modeling_object(
             entity, {.object_id = modeling_object_id,
                      .material_slot = material_slot,
                       .sdk_owned = sdk_owned,
                       .local_frame_version = local_frame_version}) &&
         scene.set_mesh(entity, mesh);
}

bool ModelingMeshSyncService::bind_projected_curve_object(
    SceneDocument &scene, EntityId entity, int modeling_object_id,
    AssetId curve_id,
    std::optional<std::uint32_t> requested_local_frame_version) const {
  const ModelingObjectComponent *current_modeling_object =
      scene.modeling_object(entity);
  const std::uint32_t local_frame_version =
      requested_local_frame_version.value_or(
          current_modeling_object != nullptr
              ? current_modeling_object->local_frame_version
              : kCanonicalModelingObjectLocalFrameVersion);
  const CurveComponent *current_curve = scene.curve(entity);
  CurveComponent curve = current_curve != nullptr ? *current_curve
                                                   : CurveComponent{};
  curve.curve = curve_id;
  return scene.set_modeling_object(
             entity, {.object_id = modeling_object_id,
                      .material_slot = 0u,
                      .sdk_owned = true,
                      .local_frame_version = local_frame_version}) &&
         scene.set_curve(entity, curve);
}

bool ModelingMeshSyncService::mark_projected_object_sdk_owned(
    SceneDocument &scene, EntityId entity) const {
  const ModelingObjectComponent *component = scene.modeling_object(entity);
  if (component == nullptr) {
    return false;
  }
  if (component->sdk_owned) {
    return true;
  }
  ModelingObjectComponent sdk_component = *component;
  sdk_component.sdk_owned = true;
  return scene.set_modeling_object(entity, sdk_component);
}

bool ModelingMeshSyncService::set_projected_mesh_binding(
    SceneDocument &scene, EntityId entity, AssetId mesh_id) const {
  return scene.set_mesh(entity, {.mesh = mesh_id});
}

bool ModelingMeshSyncService::set_projected_curve_binding(
    SceneDocument &scene, EntityId entity, AssetId curve_id) const {
  const CurveComponent *current_curve = scene.curve(entity);
  CurveComponent curve = current_curve != nullptr ? *current_curve
                                                   : CurveComponent{};
  curve.curve = curve_id;
  return scene.set_curve(entity, curve);
}

bool ModelingMeshSyncService::set_projected_material(
    SceneDocument &scene, EntityId entity,
    const MaterialComponent *material) const {
  return material != nullptr ? scene.set_material(entity, *material)
                             : scene.material(entity) == nullptr ||
                                   scene.clear_material(entity);
}

bool ModelingMeshSyncService::notify_projected_mesh_changed(
    SceneDocument &scene, EntityId entity) const {
  return scene.notify_mesh_payload_changed(entity);
}

bool ModelingMeshSyncService::notify_projected_curve_changed(
    SceneDocument &scene, EntityId entity) const {
  return scene.notify_curve_payload_changed(entity);
}

bool ModelingMeshSyncService::apply_scene_identity_to_entity(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity,
    bool include_selection, bool include_hierarchy_placement) const {
  return apply_complete_scene_identity_to_entity(
      scene, entity, identity, include_selection,
      include_hierarchy_placement);
}

bool ModelingMeshSyncService::apply_scene_identity_parent(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity) const {
  if (!scene.exists(entity)) {
    return false;
  }
  const EntityId parent =
      identity.parent != kInvalidEntityId && scene.exists(identity.parent)
          ? identity.parent
          : scene.scene_root();
  return scene.parent(entity) == parent ||
         scene.set_parent(entity, parent, false);
}

bool ModelingMeshSyncService::apply_scene_identity_sibling_ordinal(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity) const {
  if (!scene.exists(entity)) {
    return false;
  }
  const EntityId parent =
      identity.parent != kInvalidEntityId && scene.exists(identity.parent)
          ? identity.parent
          : scene.scene_root();
  if (scene.parent(entity) != parent) {
    return false;
  }
  // A missing captured parent deliberately falls back to the scene root. Its
  // ordinal belonged to a different sibling collection and cannot be applied
  // to that fallback collection.
  return parent != identity.parent || !identity.sibling_ordinal.has_value() ||
         scene.set_sibling_ordinal(entity, *identity.sibling_ordinal);
}

bool ModelingMeshSyncService::set_entity_material(
    SceneDocument &scene, EntityId entity,
    const MaterialComponent &material) const {
  const MaterialComponent *current = scene.material(entity);
  if (current != nullptr && *current == material) {
    return false;
  }
  return scene.set_material(entity, material);
}

bool ModelingMeshSyncService::restore_history_mesh_assets(
    AssetRegistry &assets,
    const std::unordered_map<AssetId, MeshAsset> &mesh_assets) const {
  std::vector<AssetId> ordered_ids;
  ordered_ids.reserve(mesh_assets.size());
  for (const auto &[id, mesh] : mesh_assets) {
    if (id == kInvalidAssetId || !assets.validate_mesh(mesh).ok) {
      return false;
    }
    ordered_ids.push_back(id);
  }
  std::sort(ordered_ids.begin(), ordered_ids.end());

  std::unordered_map<AssetId, std::optional<MeshAsset>> rollback;
  rollback.reserve(ordered_ids.size());
  std::vector<AssetId> changed_ids;
  changed_ids.reserve(ordered_ids.size());
  for (const AssetId id : ordered_ids) {
    const MeshAsset &snapshot = mesh_assets.at(id);
    const MeshAsset *current = assets.mesh(id);
    if (current != nullptr && mesh_asset_payload_equal(*current, snapshot)) {
      continue;
    }

    rollback.emplace(
        id, current != nullptr
                ? std::optional<MeshAsset>{clone_mesh_asset_payload(*current)}
                : std::nullopt);
    const bool restored =
        current != nullptr
            ? assets.replace_mesh(id, clone_mesh_asset_payload(snapshot))
            : assets.add_mesh(id, clone_mesh_asset_payload(snapshot)) == id;
    if (restored) {
      changed_ids.push_back(id);
      continue;
    }

    for (auto restored_id = changed_ids.rbegin();
         restored_id != changed_ids.rend(); ++restored_id) {
      const std::optional<MeshAsset> &previous = rollback.at(*restored_id);
      if (previous.has_value()) {
        (void)assets.replace_mesh(
            *restored_id, clone_mesh_asset_payload(*previous));
      } else {
        (void)assets.remove_mesh(*restored_id);
      }
    }
    return false;
  }
  return true;
}

void ModelingMeshSyncService::remove_unreferenced_history_mesh_assets(
    const SceneDocument &scene, AssetRegistry &assets,
    std::span<const AssetId> mesh_assets) const {
  std::vector<AssetId> ordered_ids(mesh_assets.begin(), mesh_assets.end());
  std::sort(ordered_ids.begin(), ordered_ids.end());
  ordered_ids.erase(std::unique(ordered_ids.begin(), ordered_ids.end()),
                    ordered_ids.end());
  const std::vector<EntityId> scene_entities = scene.entities();
  std::unordered_set<AssetId> live_meshes;
  live_meshes.reserve(scene_entities.size());
  for (const EntityId entity : scene_entities) {
    const MeshComponent *component = scene.mesh(entity);
    if (component != nullptr && component->mesh != kInvalidAssetId) {
      live_meshes.insert(component->mesh);
    }
  }
  for (const AssetId id : ordered_ids) {
    if (id != kInvalidAssetId && !live_meshes.contains(id)) {
      (void)assets.remove_mesh(id);
    }
  }
}

bool ModelingMeshSyncService::restore_history_curve_assets(
    AssetRegistry &assets,
    const std::unordered_map<AssetId, CurveAsset> &curve_assets) const {
  std::vector<AssetId> ordered_ids;
  ordered_ids.reserve(curve_assets.size());
  for (const auto &[id, curve] : curve_assets) {
    if (id == kInvalidAssetId || !assets.validate_curve(curve).ok) {
      return false;
    }
    ordered_ids.push_back(id);
  }
  std::sort(ordered_ids.begin(), ordered_ids.end());

  for (const AssetId id : ordered_ids) {
    const CurveAsset &snapshot = curve_assets.at(id);
    const CurveAsset *current = assets.curve(id);
    if (current != nullptr && current->name == snapshot.name &&
        current->document != nullptr &&
        current->document->identity_high_water() ==
            snapshot.document->identity_high_water() &&
        current->document->has_same_geometry(*snapshot.document)) {
      continue;
    }
    const bool restored = current != nullptr
                              ? assets.replace_curve(id, snapshot)
                              : assets.add_curve(id, snapshot) == id;
    if (!restored) {
      // The projection adapter owns the complete scene/asset checkpoint and
      // restores it if any part of this publication fails.
      return false;
    }
  }
  return true;
}

void ModelingMeshSyncService::remove_unreferenced_history_curve_assets(
    const SceneDocument &scene, AssetRegistry &assets,
    std::span<const AssetId> curve_assets) const {
  std::vector<AssetId> ordered_ids(curve_assets.begin(), curve_assets.end());
  std::sort(ordered_ids.begin(), ordered_ids.end());
  ordered_ids.erase(std::unique(ordered_ids.begin(), ordered_ids.end()),
                    ordered_ids.end());
  const std::vector<EntityId> scene_entities = scene.entities();
  std::unordered_set<AssetId> live_curves;
  live_curves.reserve(scene_entities.size());
  for (const EntityId entity : scene_entities) {
    const CurveComponent *component = scene.curve(entity);
    if (component != nullptr && component->curve != kInvalidAssetId) {
      live_curves.insert(component->curve);
    }
  }
  for (const AssetId id : ordered_ids) {
    if (id != kInvalidAssetId && !live_curves.contains(id)) {
      (void)assets.remove_curve(id);
    }
  }
}

bool ModelingMeshSyncService::apply_scene_identity_to_duplicate_entity(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity) const {
  if (!scene.exists(entity)) {
    return false;
  }
  ModelingObjectSceneIdentity duplicate_identity = identity;
  if (const ModelingObjectComponent *modeling = scene.modeling_object(entity)) {
    duplicate_identity.modeling_object = *modeling;
  }
  if (const MeshComponent *mesh = scene.mesh(entity)) {
    MeshComponent duplicate_mesh = duplicate_identity.mesh.value_or(*mesh);
    duplicate_mesh.mesh = mesh->mesh;
    duplicate_identity.mesh = duplicate_mesh;
  }
  if (const CurveComponent *curve = scene.curve(entity)) {
    CurveComponent duplicate_curve =
        duplicate_identity.curve.value_or(*curve);
    duplicate_curve.curve = curve->curve;
    duplicate_identity.curve = duplicate_curve;
  }
  duplicate_identity.instance_definition.reset();
  duplicate_identity.instance_occurrence.reset();
  duplicate_identity.scene_settings.reset();
  if (!apply_scene_entity_state(
          scene, entity, duplicate_identity,
          {.include_selection = false,
           .include_instance_roles = false,
           .include_scene_settings = false})) {
    return false;
  }
  const EntityId parent =
      identity.parent != kInvalidEntityId && scene.exists(identity.parent)
          ? identity.parent
          : scene.scene_root();
  return scene.parent(entity) == parent || scene.set_parent(entity, parent, false);
}

bool ModelingMeshSyncService::apply_scene_transforms(
    SceneDocument &scene,
    std::span<const SceneTransformAssignment> transforms) const {
  return scene.set_transforms_and_texture_mappings(transforms, {});
}

bool ModelingMeshSyncService::place_created_scene_object(
    SceneDocument &scene, EntityId entity, EntityId parent,
    const TransformComponent &local_transform) const {
  if (!scene.exists(entity) || !scene.exists(parent) ||
      !scene.set_parent(entity, parent, false)) {
    return false;
  }
  return scene.set_transform(entity, local_transform);
}

} // namespace quader

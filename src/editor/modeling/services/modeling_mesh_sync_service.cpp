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
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/services/scene_modeling_sync_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::uint64_t kProjectedMeshSignatureSeed = 1469598103934665603ull;
constexpr std::uint64_t kProjectedMeshSignaturePrime = 1099511628211ull;

std::uint64_t mix_projected_mesh_signature(std::uint64_t hash,
                                           std::uint64_t value) {
  return (hash ^ value) * kProjectedMeshSignaturePrime;
}

std::uint64_t mix_projected_mesh_signature(std::uint64_t hash, int value) {
  return mix_projected_mesh_signature(
      hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint64_t mix_projected_mesh_signature(std::uint64_t hash,
                                           std::uint32_t value) {
  return mix_projected_mesh_signature(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t mix_projected_mesh_signature(std::uint64_t hash, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return mix_projected_mesh_signature(hash, bits);
}

std::uint64_t mix_projected_mesh_signature(std::uint64_t hash, QVec2 value) {
  hash = mix_projected_mesh_signature(hash, value.x);
  hash = mix_projected_mesh_signature(hash, value.y);
  return hash;
}

std::uint64_t mix_projected_mesh_signature(std::uint64_t hash, QVec3 value) {
  hash = mix_projected_mesh_signature(hash, value.x);
  hash = mix_projected_mesh_signature(hash, value.y);
  hash = mix_projected_mesh_signature(hash, value.z);
  return hash;
}

std::uint64_t
projected_mesh_signature(const quader_editor_core::PolygonObject &object) {
  std::uint64_t hash =
      mix_projected_mesh_signature(kProjectedMeshSignatureSeed, object.id);
  hash = mix_projected_mesh_signature(
      hash, static_cast<std::uint64_t>(object.document.vertices.size()));
  hash = mix_projected_mesh_signature(
      hash, static_cast<std::uint64_t>(object.document.faces.size()));
  hash = mix_projected_mesh_signature(
      hash,
      static_cast<std::uint64_t>(object.document.hard_normal_edges.size()));
  for (const quader_poly::Vertex &vertex : object.document.vertices) {
    hash = mix_projected_mesh_signature(hash, vertex.id);
    hash = mix_projected_mesh_signature(hash, vertex.position);
  }
  for (const quader_poly::Face &face : object.document.faces) {
    hash = mix_projected_mesh_signature(hash, face.id);
    hash = mix_projected_mesh_signature(
        hash, static_cast<std::uint64_t>(face.vertices.size()));
    hash = mix_projected_mesh_signature(
        hash, static_cast<std::uint64_t>(face.uvs.size()));
    hash = mix_projected_mesh_signature(hash, face.material_slot);
    hash = mix_projected_mesh_signature(hash,
                                        static_cast<int>(face.normal_shading));
    for (const quader_poly::ElementId vertex_id : face.vertices) {
      hash = mix_projected_mesh_signature(hash, vertex_id);
    }
    for (const QVec2 uv : face.uvs) {
      hash = mix_projected_mesh_signature(hash, uv);
    }
  }
  for (const quader_poly::Edge edge : object.document.hard_normal_edges) {
    hash = mix_projected_mesh_signature(hash, edge.a);
    hash = mix_projected_mesh_signature(hash, edge.b);
  }
  return hash;
}

std::unordered_map<std::uint32_t, AssetId>
material_slots_for_live_modeling_nodes(
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_map,
    std::optional<AssetId> &first_modeling_material) {
  QDR_PROFILE_FUNCTION();
  std::unordered_map<std::uint32_t, AssetId> material_by_slot;
  const std::vector<std::pair<int, EntityId>> entities_by_object =
      object_map.object_entity_bindings_by_modeling_object();

  for (const auto &[modelingObjectId, entity] : entities_by_object) {
    (void)modelingObjectId;
    if (!scene.exists(entity)) {
      continue;
    }
    const ModelingObjectComponent *modeling_object =
        scene.modeling_object(entity);
    const MaterialComponent *material = scene.material(entity);
    if (modeling_object == nullptr || material == nullptr ||
        material->material == kInvalidAssetId) {
      continue;
    }
    if (!first_modeling_material) {
      first_modeling_material = material->material;
    }
    material_by_slot.try_emplace(modeling_object->material_slot,
                                 material->material);
  }
  return material_by_slot;
}

std::optional<MaterialComponent> material_component_for_modeling_slot(
    std::uint32_t material_slot,
    const std::unordered_map<std::uint32_t, AssetId> &material_by_slot,
    std::optional<AssetId> first_modeling_material,
    const MaterialComponent *existing_material) {
  AssetId primary_material = kInvalidAssetId;
  if (existing_material != nullptr &&
      existing_material->material != kInvalidAssetId) {
    primary_material = existing_material->material;
  } else if (const auto material = material_by_slot.find(material_slot);
             material != material_by_slot.end()) {
    primary_material = material->second;
  } else if (first_modeling_material.has_value()) {
    primary_material = *first_modeling_material;
  }
  if (primary_material == kInvalidAssetId) {
    return std::nullopt;
  }
  std::unordered_map<std::uint32_t, AssetId> resolved_slots =
      existing_material != nullptr ? existing_material->material_slots
                                   : material_by_slot;
  resolved_slots[material_slot] = primary_material;
  return MaterialComponent{
      .material = primary_material,
      .material_slots = std::move(resolved_slots),
  };
}

EntityId modeling_default_parent(const SceneDocument &scene) {
  return scene.scene_root();
}

bool scene_mesh_asset_is_shared(const SceneDocument &scene,
                                EntityId projected_entity, AssetId mesh) {
  if (mesh == kInvalidAssetId) {
    return false;
  }
  for (const EntityId entity : scene.entities()) {
    const MeshComponent *component = scene.mesh(entity);
    if (entity != projected_entity && component != nullptr &&
        component->mesh == mesh) {
      return true;
    }
  }
  return false;
}

/**
 * Describes how one prevalidated projected mesh reaches the asset registry.
 */
enum class ProjectedMeshInstallKind {
  Keep,
  Add,
  Replace,
};

/**
 * Holds every validated input and rollback snapshot for one mesh projection.
 */
struct ProjectedMeshPlan {
  int modeling_object_id = kInvalidModelingObjectId;
  std::uint32_t material_slot = 0;
  std::uint64_t signature = 0;
  std::string scene_name;
  ProjectedMeshInstallKind install_kind = ProjectedMeshInstallKind::Keep;
  AssetId installed_mesh = kInvalidAssetId;
  std::optional<MeshAsset> projected_mesh;
  std::optional<MeshAsset> mesh_before_replace;
};

const ProjectedMeshPlan *find_projected_mesh_plan(
    const std::vector<ProjectedMeshPlan> &plans, int modeling_object_id) {
  const auto plan = std::find_if(
      plans.begin(), plans.end(), [&](const ProjectedMeshPlan &candidate) {
        return candidate.modeling_object_id == modeling_object_id;
      });
  return plan == plans.end() ? nullptr : &*plan;
}

std::optional<std::vector<ProjectedMeshPlan>> build_projected_mesh_plans(
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const AssetRegistry &assets,
    const SceneModelingSyncService &scene_sync) {
  std::vector<ProjectedMeshPlan> plans;
  plans.reserve(session.objects().size());
  std::unordered_set<int> planned_objects;
  SceneEntityNameAllocator scene_names(scene);

  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    if (!planned_objects.insert(object.id).second) {
      return std::nullopt;
    }

    ProjectedMeshPlan plan;
    plan.modeling_object_id = object.id;
    plan.material_slot = static_cast<std::uint32_t>(object.material_id);
    plan.signature = projected_mesh_signature(object);

    const std::optional<EntityId> existing =
        scene_sync.object_map().entity_for_modeling_object(object.id);
    const EntityId existing_entity =
        existing.has_value() && scene.exists(*existing) ? *existing
                                                        : kInvalidEntityId;
    const std::optional<ModelingObjectSceneIdentity> identity =
        scene_sync.object_map().scene_identity_for_modeling_object(object.id);
    if (existing_entity != kInvalidEntityId) {
      if (const NameComponent *name = scene.name(existing_entity)) {
        plan.scene_name = name->name;
      }
    }
    if (plan.scene_name.empty() && identity.has_value() &&
        !identity->name.empty()) {
      plan.scene_name = identity->name;
    }
    if (plan.scene_name.empty()) {
      plan.scene_name = scene_names.next_name_for_kind(SceneEntityKind::Mesh);
      scene_names.reserve_name(plan.scene_name);
    }

    const MeshComponent *bound_mesh =
        existing_entity != kInvalidEntityId ? scene.mesh(existing_entity)
                                            : nullptr;
    const AssetId current_mesh =
        bound_mesh != nullptr ? bound_mesh->mesh : kInvalidAssetId;
    if (current_mesh != kInvalidAssetId && assets.mesh(current_mesh) != nullptr &&
        scene_sync.projected_mesh_signature_matches(object.id,
                                                    plan.signature)) {
      plan.installed_mesh = current_mesh;
      plans.push_back(std::move(plan));
      continue;
    }

    MeshAsset projected = MeshProjectionService::compile_object_mesh(object);
    projected.name = plan.scene_name;
    if (!assets.validate_mesh(projected).ok) {
      return std::nullopt;
    }

    if (const MeshAsset *current = assets.mesh(current_mesh);
        current != nullptr && mesh_asset_payload_equal(*current, projected)) {
      plan.installed_mesh = current_mesh;
    } else if (current_mesh != kInvalidAssetId && current != nullptr &&
               !scene_mesh_asset_is_shared(scene, existing_entity,
                                           current_mesh)) {
      plan.install_kind = ProjectedMeshInstallKind::Replace;
      plan.installed_mesh = current_mesh;
      plan.mesh_before_replace = clone_mesh_asset_payload(*current);
      plan.projected_mesh = std::move(projected);
    } else {
      plan.install_kind = ProjectedMeshInstallKind::Add;
      plan.projected_mesh = std::move(projected);
    }
    plans.push_back(std::move(plan));
  }
  return plans;
}

void rollback_projected_mesh_installs(
    AssetRegistry &assets, std::vector<ProjectedMeshPlan> &plans,
    std::span<const std::size_t> installed_plan_indices) {
  for (auto installed = installed_plan_indices.rbegin();
       installed != installed_plan_indices.rend(); ++installed) {
    ProjectedMeshPlan &plan = plans[*installed];
    if (plan.install_kind == ProjectedMeshInstallKind::Add) {
      (void)assets.remove_mesh(plan.installed_mesh);
      plan.installed_mesh = kInvalidAssetId;
    } else if (plan.install_kind == ProjectedMeshInstallKind::Replace &&
               plan.mesh_before_replace.has_value()) {
      (void)assets.replace_mesh(
          plan.installed_mesh,
          clone_mesh_asset_payload(*plan.mesh_before_replace));
    }
  }
}

bool install_projected_mesh_assets(AssetRegistry &assets,
                                   std::vector<ProjectedMeshPlan> &plans,
                                   std::vector<std::size_t> &installed_plans) {
  installed_plans.clear();
  installed_plans.reserve(plans.size());
  for (std::size_t index = 0; index < plans.size(); ++index) {
    ProjectedMeshPlan &plan = plans[index];
    if (plan.install_kind == ProjectedMeshInstallKind::Keep) {
      continue;
    }
    if (!plan.projected_mesh.has_value()) {
      rollback_projected_mesh_installs(assets, plans, installed_plans);
      return false;
    }

    if (plan.install_kind == ProjectedMeshInstallKind::Add) {
      plan.installed_mesh =
          assets.add_mesh(clone_mesh_asset_payload(*plan.projected_mesh));
      if (plan.installed_mesh == kInvalidAssetId) {
        rollback_projected_mesh_installs(assets, plans, installed_plans);
        return false;
      }
    } else if (!assets.replace_mesh(
                   plan.installed_mesh,
                   clone_mesh_asset_payload(*plan.projected_mesh))) {
      rollback_projected_mesh_installs(assets, plans, installed_plans);
      return false;
    }
    installed_plans.push_back(index);
  }
  return true;
}

void sync_material_slot_bindings(
    SceneDocument &scene, EntityId entity, std::uint32_t material_slot,
    const std::unordered_map<std::uint32_t, AssetId> &material_by_slot,
    std::optional<AssetId> first_modeling_material) {
  QDR_PROFILE_FUNCTION();
  const MaterialComponent *current_material = scene.material(entity);
  const std::optional<MaterialComponent> next_material =
      material_component_for_modeling_slot(material_slot, material_by_slot,
                                           first_modeling_material,
                                           current_material);
  if (!next_material.has_value()) {
    return;
  }
  if (current_material == nullptr || *current_material != *next_material) {
    scene.set_material(entity, *next_material);
  }
}

void apply_durable_scene_identity_to_entity(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity) {
  {
    QDR_PROFILE_SCOPE("Modeling mesh sync apply identity transform");
    scene.set_transform(entity, identity.transform);
  }
  const EntityId parent =
      identity.parent != kInvalidEntityId && scene.exists(identity.parent)
          ? identity.parent
          : modeling_default_parent(scene);
  if (parent != kInvalidEntityId && scene.exists(parent)) {
    QDR_PROFILE_SCOPE("Modeling mesh sync apply identity parent");
    (void)scene.set_parent(entity, parent, false);
  }
  {
    QDR_PROFILE_SCOPE("Modeling mesh sync apply identity visibility");
    scene.set_visible(entity, identity.visibility.visible);
  }
  {
    QDR_PROFILE_SCOPE("Modeling mesh sync apply identity lock");
    scene.set_locked(entity, identity.lock.locked);
  }
}

void apply_complete_scene_identity_to_entity(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity) {
  apply_durable_scene_identity_to_entity(scene, entity, identity);
  if (identity.mesh.has_value()) {
    scene.set_mesh(entity, *identity.mesh);
  }
  if (identity.material.has_value()) {
    scene.set_material(entity, *identity.material);
  } else {
    scene.clear_material(entity);
  }
}

bool scene_uses_mesh_asset(const SceneDocument &scene, AssetId mesh) {
  for (const EntityId entity : scene.entities()) {
    const MeshComponent *component = scene.mesh(entity);
    if (component != nullptr && component->mesh == mesh) {
      return true;
    }
  }
  return false;
}

bool reconcile_modeling_object_scene_nodes(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    SceneModelingSyncService &scene_sync,
    const std::vector<ProjectedMeshPlan> &mesh_plans,
    std::vector<std::pair<int, EntityId>> &created_entities) {
  QDR_PROFILE_FUNCTION();
  ModelingObjectRuntimeMap &object_map = scene_sync.object_map();
  created_entities.clear();

  std::optional<AssetId> first_modeling_material;
  const std::unordered_map<std::uint32_t, AssetId> material_by_slot =
      material_slots_for_live_modeling_nodes(scene, object_map,
                                             first_modeling_material);

  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    QDR_PROFILE_SCOPE("Modeling mesh sync reconcile object node");
    const ProjectedMeshPlan *mesh_plan =
        find_projected_mesh_plan(mesh_plans, object.id);
    if (mesh_plan == nullptr) {
      return false;
    }
    const std::uint32_t material_slot = mesh_plan->material_slot;
    const std::optional<EntityId> existing =
        object_map.entity_for_modeling_object(object.id);
    if (existing.has_value() && scene.exists(*existing)) {
      continue;
    }

    const std::optional<ModelingObjectSceneIdentity> identity = [&]() {
      QDR_PROFILE_SCOPE("Modeling mesh sync lookup scene identity");
      return object_map.scene_identity_for_modeling_object(object.id);
    }();
    const std::string &name = mesh_plan->scene_name;
    const EntityId entity = [&]() {
      QDR_PROFILE_SCOPE("Modeling mesh sync create scene entity");
      return identity.has_value() && identity->entity != kInvalidEntityId
                 ? scene.create_entity_with_id(identity->entity, name,
                                               EntityKind::Mesh,
                                               identity->editor_object_id)
                 : scene.create_entity(name, EntityKind::Mesh);
    }();
    if (entity == kInvalidEntityId) {
      return false;
    }
    if (identity.has_value() && !identity->uuid.empty()) {
      if (!scene.set_entity_uuid(entity, identity->uuid)) {
        (void)scene.destroy_entity(entity);
        return false;
      }
    }
    if (!identity.has_value()) {
      const EntityId parent = modeling_default_parent(scene);
      if (parent != kInvalidEntityId && scene.exists(parent)) {
        QDR_PROFILE_SCOPE("Modeling mesh sync set default parent");
        if (!scene.set_parent(entity, parent, false)) {
          (void)scene.destroy_entity(entity);
          return false;
        }
      }
    }

    {
      QDR_PROFILE_SCOPE("Modeling mesh sync set modeling object component");
      scene.set_modeling_object(entity, ModelingObjectComponent{
                                            .object_id = object.id,
                                            .material_slot = material_slot,
                                        });
    }
    if (identity.has_value()) {
      QDR_PROFILE_SCOPE("Modeling mesh sync apply durable scene identity");
      apply_complete_scene_identity_to_entity(scene, entity, *identity);
    }
    const MaterialComponent *identity_material =
        identity.has_value() && identity->material.has_value()
            ? &*identity->material
            : nullptr;
    {
      QDR_PROFILE_SCOPE("Modeling mesh sync set material component");
      if (const std::optional<MaterialComponent> material =
              material_component_for_modeling_slot(
                  material_slot, material_by_slot, first_modeling_material,
                  identity_material)) {
        scene.set_material(entity, *material);
      }
    }

    {
      QDR_PROFILE_SCOPE("Modeling mesh sync bind object entity");
      object_map.bind(object.id, entity);
    }
    created_entities.emplace_back(object.id, entity);
  }

  return true;
}

void finalize_reconciled_modeling_object_scene_nodes(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    SceneModelingSyncService &scene_sync) {
  ModelingObjectRuntimeMap &object_map = scene_sync.object_map();
  std::optional<AssetId> first_modeling_material;
  const std::unordered_map<std::uint32_t, AssetId> material_by_slot =
      material_slots_for_live_modeling_nodes(scene, object_map,
                                             first_modeling_material);
  std::unordered_set<int> live_objects;
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    live_objects.insert(object.id);
    const std::optional<EntityId> entity =
        object_map.entity_for_modeling_object(object.id);
    if (entity.has_value() && scene.exists(*entity)) {
      sync_material_slot_bindings(
          scene, *entity, static_cast<std::uint32_t>(object.material_id),
          material_by_slot, first_modeling_material);
    }
  }

  std::vector<int> stale_objects;
  for (const auto &[modelingObjectId, entity] :
       object_map.object_entity_bindings_by_modeling_object()) {
    QDR_PROFILE_SCOPE("Modeling mesh sync detect stale object node");
    if (!live_objects.contains(modelingObjectId)) {
      stale_objects.push_back(modelingObjectId);
    }
    (void)entity;
  }

  for (const int modeling_object_id : stale_objects) {
    QDR_PROFILE_SCOPE("Modeling mesh sync remove stale object node");
    const std::optional<EntityId> entity =
        object_map.entity_for_modeling_object(modeling_object_id);
    if (entity.has_value() && scene.exists(*entity)) {
      object_map.remember_scene_entity(modeling_object_id, scene, *entity);
      scene.destroy_entity(*entity);
    }
    object_map.unbind_object(modeling_object_id);
    scene_sync.forget_projected_mesh_signature(modeling_object_id);
  }
}

void rollback_created_modeling_scene_nodes(
    SceneDocument &scene, SceneModelingSyncService &scene_sync,
    std::span<const std::pair<int, EntityId>> created_entities) {
  ModelingObjectRuntimeMap &object_map = scene_sync.object_map();
  for (auto created = created_entities.rbegin();
       created != created_entities.rend(); ++created) {
    if (object_map.entity_for_modeling_object(created->first) ==
        created->second) {
      object_map.unbind_object(created->first);
    }
    if (scene.exists(created->second)) {
      (void)scene.destroy_entity(created->second);
    }
    scene_sync.forget_projected_mesh_signature(created->first);
  }
}

bool project_modeling_meshes_to_scene(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    const AssetRegistry &assets, SceneModelingSyncService &scene_sync,
    const std::vector<ProjectedMeshPlan> &mesh_plans) {
  QDR_PROFILE_FUNCTION();
  ModelingObjectRuntimeMap &object_registry = scene_sync.object_map();
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    const ProjectedMeshPlan *plan =
        find_projected_mesh_plan(mesh_plans, object.id);
    const std::optional<EntityId> existing =
        object_registry.entity_for_modeling_object(object.id);
    if (plan == nullptr || !existing.has_value() ||
        !scene.exists(*existing) ||
        plan->installed_mesh == kInvalidAssetId ||
        assets.mesh(plan->installed_mesh) == nullptr) {
      return false;
    }
  }

  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    QDR_PROFILE_SCOPE("Modeling mesh sync project object mesh");
    const ProjectedMeshPlan *plan =
        find_projected_mesh_plan(mesh_plans, object.id);
    const std::optional<EntityId> existing =
        object_registry.entity_for_modeling_object(object.id);
    const EntityId entity = *existing;
    const ModelingObjectComponent next_object{
        .object_id = object.id,
        .material_slot = plan->material_slot,
    };
    const ModelingObjectComponent *current_object =
        scene.modeling_object(entity);
    if (current_object == nullptr ||
        current_object->object_id != next_object.object_id ||
        current_object->material_slot != next_object.material_slot) {
      scene.set_modeling_object(entity, next_object);
    }
    const MeshComponent *current_mesh = scene.mesh(entity);
    if (current_mesh == nullptr ||
        current_mesh->mesh != plan->installed_mesh ||
        plan->install_kind == ProjectedMeshInstallKind::Replace) {
      const std::optional<ModelingObjectSceneIdentity> identity =
          object_registry.scene_identity_for_modeling_object(object.id);
      MeshComponent next_mesh =
          current_mesh != nullptr
              ? *current_mesh
              : (identity.has_value() && identity->mesh.has_value()
                     ? *identity->mesh
                     : MeshComponent{});
      next_mesh.mesh = plan->installed_mesh;
      scene.set_mesh(entity, next_mesh);
    }
    object_registry.remember_scene_entity(object.id, scene, entity);
    scene_sync.remember_projected_mesh_signature(object.id, plan->signature);
  }
  return true;
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

void ModelingMeshSyncService::apply_scene_identity_to_entity(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity) const {
  apply_complete_scene_identity_to_entity(scene, entity, identity);
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
  for (const AssetId id : ordered_ids) {
    if (id != kInvalidAssetId && !scene_uses_mesh_asset(scene, id)) {
      (void)assets.remove_mesh(id);
    }
  }
}

void ModelingMeshSyncService::apply_scene_identity_to_duplicate_entity(
    SceneDocument &scene, EntityId entity,
    const ModelingObjectSceneIdentity &identity) const {
  apply_durable_scene_identity_to_entity(scene, entity, identity);
  if (identity.mesh.has_value()) {
    if (const MeshComponent *duplicate_mesh = scene.mesh(entity)) {
      MeshComponent next_mesh = *identity.mesh;
      next_mesh.mesh = duplicate_mesh->mesh;
      scene.set_mesh(entity, next_mesh);
    }
  }
  if (identity.material.has_value()) {
    scene.set_material(entity, *identity.material);
  } else {
    scene.clear_material(entity);
  }
}

void ModelingMeshSyncService::apply_scene_transforms(
    SceneDocument &scene,
    std::span<const SceneTransformAssignment> transforms) const {
  scene.set_transforms(transforms);
}

bool ModelingMeshSyncService::sync_scene_objects_from_modeling(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    AssetRegistry &assets, SceneModelingSyncService &scene_sync,
    bool force) const {
  QDR_PROFILE_FUNCTION();
  if (!force && scene_sync.scene_objects_synced_for_current_modeling_content(
                    session, scene, assets)) {
    return true;
  }

  std::optional<std::vector<ProjectedMeshPlan>> mesh_plans =
      build_projected_mesh_plans(session, scene, assets, scene_sync);
  if (!mesh_plans.has_value()) {
    scene_sync.invalidate_scene_objects();
    return false;
  }

  std::vector<std::size_t> installed_plans;
  if (!install_projected_mesh_assets(assets, *mesh_plans, installed_plans)) {
    scene_sync.invalidate_scene_objects();
    return false;
  }

  std::vector<std::pair<int, EntityId>> created_entities;
  if (!reconcile_modeling_object_scene_nodes(
          session, scene, scene_sync, *mesh_plans, created_entities)) {
    rollback_created_modeling_scene_nodes(scene, scene_sync,
                                          created_entities);
    rollback_projected_mesh_installs(assets, *mesh_plans, installed_plans);
    scene_sync.invalidate_scene_objects();
    return false;
  }

  if (!project_modeling_meshes_to_scene(session, scene, assets, scene_sync,
                                        *mesh_plans)) {
    rollback_created_modeling_scene_nodes(scene, scene_sync,
                                          created_entities);
    rollback_projected_mesh_installs(assets, *mesh_plans, installed_plans);
    scene_sync.invalidate_scene_objects();
    return false;
  }

  finalize_reconciled_modeling_object_scene_nodes(session, scene, scene_sync);
  scene_sync.mark_scene_objects_synced(session.state().content_revision);
  return true;
}

ModelingCreateBoxResult
ModelingMeshSyncService::create_scene_object_from_latest(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    AssetRegistry &assets, SceneModelingSyncService &scene_sync,
    const std::string &name, const std::string &message) const {
  const auto failure = [&](std::string failure_message) {
    scene_sync.invalidate_scene_objects();
    return ModelingCreateBoxResult{
        .ok = false,
        .changed = false,
        .message = std::move(failure_message),
    };
  };
  if (session.objects().empty()) {
    return failure("Modeling runtime did not provide an object to project.");
  }

  const quader_editor_core::PolygonObject &object = session.objects().back();
  MeshAsset mesh = MeshProjectionService::compile_object_mesh(object);
  mesh.name = name;
  const AssetRegistryValidationResult validation = assets.validate_mesh(mesh);
  if (!validation.ok) {
    return failure(validation.message.empty()
                       ? "Modeling mesh projection is invalid."
                       : validation.message);
  }
  const AssetId mesh_id = assets.add_mesh(std::move(mesh));
  if (mesh_id == kInvalidAssetId) {
    return failure("Modeling mesh asset could not be installed.");
  }

  const EntityId entity = scene.create_entity(name);
  if (entity == kInvalidEntityId) {
    (void)assets.remove_mesh(mesh_id);
    return failure("Scene entity could not be created.");
  }
  const EntityId parent = modeling_default_parent(scene);
  if (parent != kInvalidEntityId && scene.exists(parent)) {
    if (!scene.set_parent(entity, parent, false)) {
      (void)scene.destroy_entity(entity);
      (void)assets.remove_mesh(mesh_id);
      return failure("Scene entity could not be parented.");
    }
  }
  scene.set_modeling_object(
      entity,
      ModelingObjectComponent{
          .object_id = object.id,
          .material_slot = static_cast<std::uint32_t>(object.material_id),
      });
  scene.set_mesh(entity, MeshComponent{.mesh = mesh_id});
  const MeshComponent *installed_mesh = scene.mesh(entity);
  if (installed_mesh == nullptr || installed_mesh->mesh != mesh_id) {
    (void)scene.destroy_entity(entity);
    (void)assets.remove_mesh(mesh_id);
    return failure("Scene mesh binding could not be installed.");
  }

  const std::vector<EntityId> selected_before = scene.selected_entities();
  const EntityId active_before = scene.primary_selection();
  scene.clear_selection();
  if (!scene.set_selected(entity, true)) {
    (void)scene.destroy_entity(entity);
    (void)assets.remove_mesh(mesh_id);
    scene.clear_selection();
    for (const EntityId selected : selected_before) {
      if (scene.exists(selected)) {
        (void)scene.set_selected(selected, true);
      }
    }
    if (active_before != kInvalidEntityId && scene.exists(active_before)) {
      (void)scene.set_active_selection(active_before);
    }
    return failure("Scene selection could not be updated.");
  }

  scene_sync.object_map().bind(object.id, entity);
  scene_sync.remember_projected_mesh_signature(
      object.id, projected_mesh_signature(object));
  scene_sync.mark_scene_objects_synced(session.state().content_revision);

  return {
      .ok = true,
      .changed = true,
      .message = message,
      .object =
          {
              .modeling_object_id = object.id,
              .entity = entity,
              .mesh = mesh_id,
              .name = name,
          },
  };
}

} // namespace quader

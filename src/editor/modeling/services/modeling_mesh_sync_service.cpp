////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"

#include "assets/asset_registry.hpp"
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
  return MaterialComponent{
      .material = primary_material,
      .material_slots = material_by_slot,
  };
}

EntityId modeling_default_parent(const SceneDocument &scene) {
  return scene.scene_root();
}

bool vec2_equals(QVec2 left, QVec2 right) {
  return left.x == right.x && left.y == right.y;
}

bool vec3_equals(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool mesh_vertices_equal(const std::vector<MeshVertex> &left,
                         const std::vector<MeshVertex> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (!vec3_equals(left[index].position, right[index].position) ||
        !vec3_equals(left[index].normal, right[index].normal) ||
        !vec2_equals(left[index].uv, right[index].uv) ||
        left[index].rgba != right[index].rgba) {
      return false;
    }
  }
  return true;
}

bool mesh_primitive_spans_equal(const MeshAsset &left, const MeshAsset &right) {
  const std::vector<MeshPrimitiveSpan> left_spans =
      normalized_mesh_primitive_spans(left);
  const std::vector<MeshPrimitiveSpan> right_spans =
      normalized_mesh_primitive_spans(right);
  if (left_spans.size() != right_spans.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left_spans.size(); ++index) {
    if (left_spans[index].index_offset != right_spans[index].index_offset ||
        left_spans[index].index_count != right_spans[index].index_count ||
        left_spans[index].material_slot != right_spans[index].material_slot) {
      return false;
    }
  }
  return true;
}

bool polygon_vertices_equal(const std::vector<quader_poly::Vertex> &left,
                            const std::vector<quader_poly::Vertex> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].id != right[index].id ||
        !vec3_equals(left[index].position, right[index].position)) {
      return false;
    }
  }
  return true;
}

bool polygon_faces_equal(const std::vector<quader_poly::Face> &left,
                         const std::vector<quader_poly::Face> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].id != right[index].id ||
        left[index].vertices != right[index].vertices ||
        left[index].material_slot != right[index].material_slot ||
        left[index].normal_shading != right[index].normal_shading ||
        left[index].uvs.size() != right[index].uvs.size()) {
      return false;
    }
    for (std::size_t uv_index = 0; uv_index < left[index].uvs.size();
         ++uv_index) {
      if (!vec2_equals(left[index].uvs[uv_index], right[index].uvs[uv_index])) {
        return false;
      }
    }
  }
  return true;
}

bool polygon_documents_equal(const quader_poly::Document &left,
                             const quader_poly::Document &right) {
  return polygon_vertices_equal(left.vertices, right.vertices) &&
         polygon_faces_equal(left.faces, right.faces) &&
         left.hard_normal_edges == right.hard_normal_edges &&
         left.next_vertex_id == right.next_vertex_id &&
         left.next_face_id == right.next_face_id;
}

bool authored_polygon_documents_equal(const MeshAsset &left,
                                      const MeshAsset &right) {
  if (left.has_authored_polygon_document !=
      right.has_authored_polygon_document) {
    return false;
  }
  if (!left.has_authored_polygon_document) {
    return true;
  }
  if (!left.authored_polygon_document || !right.authored_polygon_document) {
    return false;
  }
  return polygon_documents_equal(*left.authored_polygon_document,
                                 *right.authored_polygon_document);
}

bool modeling_mesh_assets_equal(const MeshAsset &left, const MeshAsset &right) {
  return left.name == right.name &&
         left.has_stable_cpu_topology == right.has_stable_cpu_topology &&
         mesh_vertices_equal(left.vertices, right.vertices) &&
         left.indices == right.indices &&
         mesh_primitive_spans_equal(left, right) &&
         authored_polygon_documents_equal(left, right);
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

void reconcile_modeling_object_scene_nodes(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    SceneModelingSyncService &scene_sync) {
  QDR_PROFILE_FUNCTION();
  ModelingObjectRuntimeMap &object_map = scene_sync.object_map();

  std::optional<AssetId> first_modeling_material;
  const std::unordered_map<std::uint32_t, AssetId> material_by_slot =
      material_slots_for_live_modeling_nodes(scene, object_map,
                                             first_modeling_material);
  SceneEntityNameAllocator scene_names(scene);

  std::unordered_set<int> live_objects;
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    QDR_PROFILE_SCOPE("Modeling mesh sync reconcile object node");
    live_objects.insert(object.id);
    const std::uint32_t material_slot =
        static_cast<std::uint32_t>(object.material_id);
    const std::optional<EntityId> existing =
        object_map.entity_for_modeling_object(object.id);
    if (existing.has_value() && scene.exists(*existing)) {
      sync_material_slot_bindings(scene, *existing, material_slot,
                                  material_by_slot, first_modeling_material);
      continue;
    }

    const std::optional<ModelingObjectSceneIdentity> identity = [&]() {
      QDR_PROFILE_SCOPE("Modeling mesh sync lookup scene identity");
      return object_map.scene_identity_for_modeling_object(object.id);
    }();
    const std::string name = [&]() {
      QDR_PROFILE_SCOPE("Modeling mesh sync resolve scene name");
      return identity.has_value() && !identity->name.empty()
                 ? identity->name
                 : scene_names.next_name_for_kind(SceneEntityKind::Mesh);
    }();
    const EntityId entity = [&]() {
      QDR_PROFILE_SCOPE("Modeling mesh sync create scene entity");
      return identity.has_value() && identity->entity != kInvalidEntityId
                 ? scene.create_entity_with_id(identity->entity, name,
                                               EntityKind::Mesh,
                                               identity->editor_object_id)
                 : scene.create_entity(name, EntityKind::Mesh);
    }();
    if (entity == kInvalidEntityId) {
      continue;
    }
    if (identity.has_value() && !identity->uuid.empty()) {
      (void)scene.set_entity_uuid(entity, identity->uuid);
    }
    scene_names.reserve_name(name);
    if (!identity.has_value()) {
      const EntityId parent = modeling_default_parent(scene);
      if (parent != kInvalidEntityId && scene.exists(parent)) {
        QDR_PROFILE_SCOPE("Modeling mesh sync set default parent");
        (void)scene.set_parent(entity, parent, false);
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
      apply_durable_scene_identity_to_entity(scene, entity, *identity);
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

void project_modeling_meshes_to_scene(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    AssetRegistry &assets, SceneModelingSyncService &scene_sync, bool force) {
  QDR_PROFILE_FUNCTION();
  if (!force && scene_sync.scene_objects_synced_for_current_modeling_content(
                    session, scene)) {
    return;
  }

  ModelingObjectRuntimeMap &object_registry = scene_sync.object_map();
  bool projected_all_objects = true;
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    QDR_PROFILE_SCOPE("Modeling mesh sync project object mesh");
    const std::uint32_t material_slot =
        static_cast<std::uint32_t>(object.material_id);
    const std::uint64_t mesh_signature = projected_mesh_signature(object);
    const std::optional<EntityId> existing =
        object_registry.entity_for_modeling_object(object.id);
    if (!existing.has_value() || !scene.exists(*existing)) {
      projected_all_objects = false;
      continue;
    }

    const EntityId entity = *existing;
    const ModelingObjectComponent next_object{
        .object_id = object.id,
        .material_slot = material_slot,
    };
    const ModelingObjectComponent *current_object =
        scene.modeling_object(entity);
    const bool had_projected_mesh_signature =
        scene_sync.projected_mesh_signature_known(object.id);
    if (!force) {
      const MeshComponent *current_mesh = scene.mesh(entity);
      if (current_mesh != nullptr && current_mesh->mesh != kInvalidAssetId &&
          assets.mesh(current_mesh->mesh) != nullptr &&
          scene_sync.projected_mesh_signature_matches(object.id,
                                                      mesh_signature)) {
        if (current_object == nullptr ||
            current_object->object_id != next_object.object_id ||
            current_object->material_slot != next_object.material_slot) {
          scene.set_modeling_object(entity, next_object);
        }
        object_registry.remember_scene_entity(object.id, scene, entity);
        continue;
      }
    }

    MeshAsset mesh = [&]() {
      QDR_PROFILE_SCOPE("Modeling mesh sync compile object mesh");
      return MeshProjectionService::compile_object_mesh(object);
    }();
    if (const NameComponent *name = scene.name(entity)) {
      mesh.name = name->name;
    }
    AssetId mesh_id = kInvalidAssetId;
    if (const MeshComponent *mesh_component = scene.mesh(entity)) {
      mesh_id = mesh_component->mesh;
    }
    bool mesh_contents_replaced = false;
    if (mesh_id == kInvalidAssetId) {
      QDR_PROFILE_SCOPE("Modeling mesh sync add mesh asset");
      mesh_id = assets.add_mesh(std::move(mesh));
    } else if (!force && had_projected_mesh_signature) {
      const bool replaced = [&]() {
        QDR_PROFILE_SCOPE("Modeling mesh sync replace changed mesh asset");
        return assets.replace_mesh(mesh_id, std::move(mesh));
      }();
      if (replaced) {
        mesh_contents_replaced = true;
      } else {
        MeshAsset replacement = [&]() {
          QDR_PROFILE_SCOPE(
              "Modeling mesh sync compile changed replacement mesh");
          return MeshProjectionService::compile_object_mesh(object);
        }();
        if (const NameComponent *name = scene.name(entity)) {
          replacement.name = name->name;
        }
        QDR_PROFILE_SCOPE(
            "Modeling mesh sync add changed replacement mesh asset");
        mesh_id = assets.add_mesh(std::move(replacement));
      }
    } else if (const MeshAsset *current_mesh = assets.mesh(mesh_id);
               current_mesh != nullptr &&
               modeling_mesh_assets_equal(*current_mesh, mesh)) {
      QDR_PROFILE_SCOPE("Modeling mesh sync keep unchanged mesh asset");
    } else if (assets.mesh(mesh_id) == nullptr) {
      QDR_PROFILE_SCOPE("Modeling mesh sync add replacement mesh asset");
      mesh_id = assets.add_mesh(std::move(mesh));
      mesh_contents_replaced = true;
    } else {
      const bool replaced = [&]() {
        QDR_PROFILE_SCOPE("Modeling mesh sync replace mesh asset");
        return assets.replace_mesh(mesh_id, std::move(mesh));
      }();
      if (replaced) {
        mesh_contents_replaced = true;
      } else {
        MeshAsset replacement = [&]() {
          QDR_PROFILE_SCOPE("Modeling mesh sync compile replacement mesh");
          return MeshProjectionService::compile_object_mesh(object);
        }();
        if (const NameComponent *name = scene.name(entity)) {
          replacement.name = name->name;
        }
        QDR_PROFILE_SCOPE("Modeling mesh sync add replacement mesh asset");
        mesh_id = assets.add_mesh(std::move(replacement));
      }
    }
    if (current_object == nullptr ||
        current_object->object_id != next_object.object_id ||
        current_object->material_slot != next_object.material_slot) {
      scene.set_modeling_object(entity, next_object);
    }
    const MeshComponent *current_mesh = scene.mesh(entity);
    if (current_mesh == nullptr || current_mesh->mesh != mesh_id ||
        mesh_contents_replaced) {
      const std::optional<ModelingObjectSceneIdentity> identity =
          object_registry.scene_identity_for_modeling_object(object.id);
      MeshComponent next_mesh =
          current_mesh != nullptr
              ? *current_mesh
              : (identity.has_value() && identity->mesh.has_value()
                     ? *identity->mesh
                     : MeshComponent{});
      next_mesh.mesh = mesh_id;
      scene.set_mesh(entity, next_mesh);
    }
    object_registry.remember_scene_entity(object.id, scene, entity);
    scene_sync.remember_projected_mesh_signature(object.id, mesh_signature);
  }
  if (projected_all_objects) {
    scene_sync.mark_scene_objects_synced(session.state().content_revision);
  }
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
  apply_durable_scene_identity_to_entity(scene, entity, identity);
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

void ModelingMeshSyncService::sync_scene_objects_from_modeling(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    AssetRegistry &assets, SceneModelingSyncService &scene_sync,
    bool force) const {
  QDR_PROFILE_FUNCTION();
  if (!force && scene_sync.scene_objects_synced_for_current_modeling_content(
                    session, scene)) {
    return;
  }
  {
    QDR_PROFILE_SCOPE("Modeling mesh sync reconcile scene nodes");
    reconcile_modeling_object_scene_nodes(session, scene, scene_sync);
  }
  {
    QDR_PROFILE_SCOPE("Modeling mesh sync project meshes to scene");
    project_modeling_meshes_to_scene(session, scene, assets, scene_sync, force);
  }
}

ModelingCreateBoxResult
ModelingMeshSyncService::create_scene_object_from_latest(
    const quader_editor_core::EditorSession &session, SceneDocument &scene,
    AssetRegistry &assets, SceneModelingSyncService &scene_sync,
    const std::string &name, const std::string &message) const {
  const quader_editor_core::PolygonObject &object = session.objects().back();
  MeshAsset mesh = MeshProjectionService::compile_object_mesh(object);
  mesh.name = name;
  const AssetId mesh_id = assets.add_mesh(std::move(mesh));

  const EntityId entity = scene.create_entity(name);
  const EntityId parent = modeling_default_parent(scene);
  if (parent != kInvalidEntityId && scene.exists(parent)) {
    (void)scene.set_parent(entity, parent, false);
  }
  scene.set_modeling_object(
      entity,
      ModelingObjectComponent{
          .object_id = object.id,
          .material_slot = static_cast<std::uint32_t>(object.material_id),
      });
  scene.set_mesh(entity, MeshComponent{.mesh = mesh_id});
  scene.clear_selection();
  scene.set_selected(entity, true);

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

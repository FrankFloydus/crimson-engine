////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"

#include "scene/scene_document.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

ModelingObjectSceneIdentity capture_scene_identity(const SceneDocument &scene,
                                                   EntityId entity) {
  ModelingObjectSceneIdentity identity;
  identity.entity = entity;
  identity.editor_object_id = scene.editor_object_id(entity);
  identity.uuid = scene.uuid(entity);
  if (const NameComponent *name = scene.name(entity)) {
    identity.name = name->name;
  }
  identity.parent = scene.parent(entity);
  if (const TransformComponent *transform = scene.transform(entity)) {
    identity.transform = *transform;
  }
  if (const VisibilityComponent *visibility = scene.visibility(entity)) {
    identity.visibility = *visibility;
  }
  if (const LockComponent *lock = scene.lock(entity)) {
    identity.lock = *lock;
  }
  if (const MeshComponent *mesh = scene.mesh(entity)) {
    identity.mesh = *mesh;
  }
  if (const MaterialComponent *material = scene.material(entity)) {
    identity.material = *material;
  }
  return identity;
}

} // namespace

std::size_t ModelingObjectRuntimeMap::size() const {
  return entities_by_object_.size();
}

bool ModelingObjectRuntimeMap::empty() const {
  return entities_by_object_.empty();
}

std::uint64_t ModelingObjectRuntimeMap::revision() const { return revision_; }

std::optional<EntityId> ModelingObjectRuntimeMap::entity_for_modeling_object(
    int modeling_object_id) const {
  const auto it = entities_by_object_.find(modeling_object_id);
  if (it == entities_by_object_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<int>
ModelingObjectRuntimeMap::modeling_object_for_entity(EntityId entity) const {
  const auto it = objects_by_entity_.find(entity);
  if (it == objects_by_entity_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<std::pair<int, EntityId>>
ModelingObjectRuntimeMap::object_entity_bindings_by_modeling_object() const {
  std::vector<std::pair<int, EntityId>> bindings(entities_by_object_.begin(),
                                                 entities_by_object_.end());
  std::sort(
      bindings.begin(), bindings.end(),
      [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
  return bindings;
}

std::optional<ModelingObjectSceneIdentity>
ModelingObjectRuntimeMap::scene_identity_for_modeling_object(
    int modeling_object_id) const {
  const auto it = scene_identity_by_object_.find(modeling_object_id);
  if (it == scene_identity_by_object_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::unordered_map<int, ModelingObjectSceneIdentity>
ModelingObjectRuntimeMap::capture_scene_identities_for_modeling_objects(
    const SceneDocument &scene,
    const std::unordered_set<int> &modeling_object_ids) const {
  std::unordered_map<int, ModelingObjectSceneIdentity> identities;
  identities.reserve(modeling_object_ids.size());
  for (const int modeling_object_id : modeling_object_ids) {
    const auto entity = entities_by_object_.find(modeling_object_id);
    if (entity != entities_by_object_.end() && scene.exists(entity->second)) {
      identities.emplace(modeling_object_id,
                         capture_scene_identity(scene, entity->second));
      continue;
    }
    const auto identity = scene_identity_by_object_.find(modeling_object_id);
    if (identity != scene_identity_by_object_.end()) {
      identities.emplace(modeling_object_id, identity->second);
    }
  }
  return identities;
}

std::vector<ModelingObjectSceneIdentity>
ModelingObjectRuntimeMap::capture_scene_identities_for_modeling_objects(
    const SceneDocument &scene,
    std::span<const int> modeling_object_ids) const {
  std::vector<ModelingObjectSceneIdentity> identities;
  identities.reserve(modeling_object_ids.size());
  for (const int modeling_object_id : modeling_object_ids) {
    const auto entity = entities_by_object_.find(modeling_object_id);
    if (entity != entities_by_object_.end() && scene.exists(entity->second)) {
      identities.push_back(capture_scene_identity(scene, entity->second));
      continue;
    }
    const auto identity = scene_identity_by_object_.find(modeling_object_id);
    if (identity != scene_identity_by_object_.end()) {
      identities.push_back(identity->second);
    }
  }
  return identities;
}

std::vector<ModelingObjectRecord> ModelingObjectRuntimeMap::records(const SceneDocument& scene) const {
    std::vector<ModelingObjectRecord> records;
    records.reserve(entities_by_object_.size());
    for (const auto &[modelingObjectId, entity] : entities_by_object_) {
      if (!scene.exists(entity)) {
        continue;
      }
      ModelingObjectRecord record;
      record.modeling_object_id = modelingObjectId;
      record.entity = entity;
      if (const MeshComponent *mesh = scene.mesh(entity)) {
        record.mesh = mesh->mesh;
      }
      if (const NameComponent *name = scene.name(entity)) {
        record.name = name->name;
      }
      records.push_back(std::move(record));
    }
    std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return records;
}

void ModelingObjectRuntimeMap::bind(int modeling_object_id, EntityId entity) {
  const auto current_entity = entities_by_object_.find(modeling_object_id);
  const auto current_object = objects_by_entity_.find(entity);
  if (current_entity != entities_by_object_.end() &&
      current_entity->second == entity &&
      current_object != objects_by_entity_.end() &&
      current_object->second == modeling_object_id) {
    return;
  }
  if (const auto existing_entity = entities_by_object_.find(modeling_object_id);
      existing_entity != entities_by_object_.end() &&
      existing_entity->second != entity) {
    const auto reverse = objects_by_entity_.find(existing_entity->second);
    if (reverse != objects_by_entity_.end() &&
        reverse->second == modeling_object_id) {
      objects_by_entity_.erase(reverse);
    }
  }
  if (const auto existing_object = objects_by_entity_.find(entity);
      existing_object != objects_by_entity_.end() &&
      existing_object->second != modeling_object_id) {
    const auto forward = entities_by_object_.find(existing_object->second);
    if (forward != entities_by_object_.end() && forward->second == entity) {
      entities_by_object_.erase(forward);
    }
  }
  entities_by_object_[modeling_object_id] = entity;
  objects_by_entity_[entity] = modeling_object_id;
  ++revision_;
}

void ModelingObjectRuntimeMap::remember_scene_entity(int modeling_object_id,
                                                     const SceneDocument &scene,
                                                     EntityId entity) {
  if (modeling_object_id == kInvalidModelingObjectId ||
      entity == kInvalidEntityId || !scene.exists(entity)) {
    return;
  }

  scene_identity_by_object_[modeling_object_id] =
      capture_scene_identity(scene, entity);
}

void ModelingObjectRuntimeMap::restore_scene_identities(
    std::unordered_map<int, ModelingObjectSceneIdentity> identities) {
  scene_identity_by_object_ = std::move(identities);
}

void ModelingObjectRuntimeMap::unbind_object(int modeling_object_id) {
  const auto it = entities_by_object_.find(modeling_object_id);
  if (it == entities_by_object_.end()) {
    return;
  }
  objects_by_entity_.erase(it->second);
  entities_by_object_.erase(it);
  ++revision_;
}

void ModelingObjectRuntimeMap::clear() {
  const bool bindings_changed =
      !entities_by_object_.empty() || !objects_by_entity_.empty();
  entities_by_object_.clear();
  objects_by_entity_.clear();
  scene_identity_by_object_.clear();
  if (bindings_changed) {
    ++revision_;
  }
}

} // namespace quader

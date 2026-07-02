////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "scene/components.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

/**
 * Stores the Document Selection State data contract used by the editor document command layer.
 */
struct DocumentSelectionState {
    std::vector<EntityId> selected;
    EntityId active = kInvalidEntityId;
};

/**
 * Stores the Document Entity Snapshot data contract used by the editor document command layer.
 */
struct DocumentEntitySnapshot {
  EntityId original_id = kInvalidEntityId;
  std::string uuid;
  EditorObjectId editor_object_id = kInvalidEditorObjectId;
  EntityId original_parent = kInvalidEntityId;
  std::string name;
  EntityKind kind = EntityKind::Generic;
  TransformComponent transform;
  VisibilityComponent visibility;
  LockComponent lock;
  SelectionComponent selection;
  std::optional<ModelingObjectComponent> modeling_object;
  std::optional<MeshComponent> mesh;
  std::optional<MaterialComponent> material;
  std::optional<LightComponent> light;
  std::vector<DocumentEntitySnapshot> children;
};

/**
 * Represents a Document Entity Placement value used by the editor document command layer.
 */
struct DocumentEntityPlacement {
  EntityId entity = kInvalidEntityId;
  EntityId parent = kInvalidEntityId;
  TransformComponent transform;
};

inline std::vector<EntityId>
document_command_normalize_existing_entities(const SceneDocument &scene,
                                             std::vector<EntityId> entities) {
  std::vector<EntityId> result;
  result.reserve(entities.size());
  for (const EntityId entity : entities) {
    if (entity != kInvalidEntityId && scene.exists(entity)) {
      result.push_back(entity);
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

inline DocumentSelectionState
document_command_capture_selection(const SceneDocument &scene) {
  return {
      .selected = scene.selected_entities(),
      .active = scene.primary_selection(),
  };
}

inline DocumentSelectionState
document_command_selection_for(const SceneDocument &scene,
                               const std::vector<EntityId> &entities,
                               EntityId active = kInvalidEntityId) {
  EntityId resolved_active = active;
  if (resolved_active == kInvalidEntityId) {
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
      if (*it != kInvalidEntityId && scene.exists(*it)) {
        resolved_active = *it;
        break;
      }
    }
  }

  std::vector<EntityId> selected =
      document_command_normalize_existing_entities(scene, entities);
  if (std::find(selected.begin(), selected.end(), resolved_active) ==
      selected.end()) {
    resolved_active = selected.empty() ? kInvalidEntityId : selected.front();
  }
  return {
      .selected = std::move(selected),
      .active = resolved_active,
  };
}

inline bool
document_command_same_selection(const DocumentSelectionState &left,
                                const DocumentSelectionState &right) {
  return left.active == right.active && left.selected == right.selected;
}

inline void
document_command_apply_selection(SceneDocument &scene,
                                 const DocumentSelectionState &state) {
  scene.clear_selection();
  for (const EntityId entity : state.selected) {
    if (scene.exists(entity)) {
      scene.set_selected(entity, true);
    }
  }
  if (state.active != kInvalidEntityId && scene.exists(state.active)) {
    scene.set_active_selection(state.active);
  }
}

inline EntityId document_command_remap_entity(
    EntityId entity,
    const std::unordered_map<EntityId, EntityId> &remapped_entities) {
  const auto mapped = remapped_entities.find(entity);
  return mapped == remapped_entities.end() ? entity : mapped->second;
}

inline DocumentSelectionState document_command_remap_selection(
    const DocumentSelectionState &state,
    const std::unordered_map<EntityId, EntityId> &remapped_entities) {
  DocumentSelectionState result;
  result.selected.reserve(state.selected.size());
  for (const EntityId entity : state.selected) {
    result.selected.push_back(
        document_command_remap_entity(entity, remapped_entities));
  }
  std::sort(result.selected.begin(), result.selected.end());
  result.selected.erase(
      std::unique(result.selected.begin(), result.selected.end()),
      result.selected.end());
  result.active =
      document_command_remap_entity(state.active, remapped_entities);
  return result;
}

inline std::optional<DocumentEntitySnapshot>
document_command_capture_entity_snapshot(const SceneDocument &scene,
                                         EntityId entity) {
  if (entity == kInvalidEntityId || !scene.exists(entity)) {
    return std::nullopt;
  }

  DocumentEntitySnapshot snapshot;
  snapshot.original_id = entity;
  snapshot.uuid = scene.uuid(entity);
  snapshot.editor_object_id = scene.editor_object_id(entity);
  snapshot.original_parent = scene.parent(entity);
  snapshot.kind = scene.kind(entity);
  if (const NameComponent *component = scene.name(entity)) {
    snapshot.name = component->name;
  }
  if (const TransformComponent *component = scene.transform(entity)) {
    snapshot.transform = *component;
  }
  if (const VisibilityComponent *component = scene.visibility(entity)) {
    snapshot.visibility = *component;
  }
  if (const LockComponent *component = scene.lock(entity)) {
    snapshot.lock = *component;
  }
  if (const SelectionComponent *component = scene.selection(entity)) {
    snapshot.selection = *component;
  }
  if (const ModelingObjectComponent *component =
          scene.modeling_object(entity)) {
    snapshot.modeling_object = *component;
  }
  if (const MeshComponent *component = scene.mesh(entity)) {
    snapshot.mesh = *component;
  }
  if (const MaterialComponent *component = scene.material(entity)) {
    snapshot.material = *component;
  }
  if (const LightComponent *component = scene.light(entity)) {
    snapshot.light = *component;
  }

  const std::vector<EntityId> children = scene.children(entity);
  snapshot.children.reserve(children.size());
  for (const EntityId child : children) {
    if (std::optional<DocumentEntitySnapshot> child_snapshot =
            document_command_capture_entity_snapshot(scene, child)) {
      snapshot.children.push_back(std::move(*child_snapshot));
    }
  }
  return snapshot;
}

inline void document_command_apply_entity_snapshot_components(
    SceneDocument &scene, EntityId entity,
    const DocumentEntitySnapshot &snapshot, bool include_modeling_object) {
  scene.set_transform(entity, snapshot.transform);
  scene.set_visible(entity, snapshot.visibility.visible);
  scene.set_locked(entity, snapshot.lock.locked);
  if (snapshot.mesh.has_value()) {
    scene.set_mesh(entity, *snapshot.mesh);
  }
  if (snapshot.material.has_value()) {
    scene.set_material(entity, *snapshot.material);
  }
  if (snapshot.light.has_value()) {
    scene.set_light(entity, *snapshot.light);
  }
  if (include_modeling_object && snapshot.modeling_object.has_value()) {
    scene.set_modeling_object(entity, *snapshot.modeling_object);
  }
  scene.set_entity_kind(entity, snapshot.kind);
}

inline void document_command_apply_entity_snapshot_common_state(
    SceneDocument &scene, EntityId entity,
    const DocumentEntitySnapshot &snapshot, std::string name) {
  scene.rename_entity(entity, std::move(name));
  scene.set_transform(entity, snapshot.transform);
  scene.set_visible(entity, snapshot.visibility.visible);
  scene.set_locked(entity, snapshot.lock.locked);
  scene.set_entity_kind(entity, snapshot.kind);
}

inline EntityId document_command_create_entity_from_snapshot(
    SceneDocument &scene, const DocumentEntitySnapshot &snapshot,
    EntityId parent, std::string name, bool include_modeling_object,
    bool preserve_identity = false) {
  EntityId entity =
      preserve_identity
          ? scene.create_entity_with_id(
                snapshot.original_id,
                name.empty() ? snapshot.name : std::move(name), snapshot.kind,
                snapshot.editor_object_id)
          : scene.create_entity(name.empty() ? snapshot.name : std::move(name),
                                snapshot.kind);
  if (entity == kInvalidEntityId) {
    return kInvalidEntityId;
  }
  if (preserve_identity && !snapshot.uuid.empty() &&
      !scene.set_entity_uuid(entity, snapshot.uuid)) {
    scene.destroy_entity(entity);
    return kInvalidEntityId;
  }
  if (parent != kInvalidEntityId && scene.exists(parent)) {
    scene.set_parent(entity, parent, false);
  }
  document_command_apply_entity_snapshot_components(scene, entity, snapshot,
                                                    include_modeling_object);
  return entity;
}

inline EntityId document_command_duplicate_entity_snapshot_tree(
    SceneDocument &scene, const DocumentEntitySnapshot &snapshot,
    EntityId parent, bool include_modeling_object = false,
    const std::unordered_map<EntityId, EntityId> *existing_clones_by_source =
        nullptr,
    SceneEntityNameAllocator *name_allocator = nullptr) {
  EntityId duplicate = kInvalidEntityId;
  const std::string duplicate_name =
      name_allocator != nullptr
          ? name_allocator->next_name(snapshot.name)
          : SceneEntityNaming::next_name(scene, snapshot.name);
  if (existing_clones_by_source != nullptr) {
    const auto existing = existing_clones_by_source->find(snapshot.original_id);
    if (existing != existing_clones_by_source->end()) {
      duplicate = existing->second;
      if (duplicate == kInvalidEntityId || !scene.exists(duplicate)) {
        return kInvalidEntityId;
      }
      if (scene.parent(duplicate) != parent &&
          !scene.set_parent(duplicate, parent, false)) {
        return kInvalidEntityId;
      }
      document_command_apply_entity_snapshot_common_state(
          scene, duplicate, snapshot, duplicate_name);
    }
  }

  if (duplicate == kInvalidEntityId) {
    duplicate = document_command_create_entity_from_snapshot(
        scene, snapshot, parent, duplicate_name, include_modeling_object);
    if (duplicate == kInvalidEntityId) {
      return kInvalidEntityId;
    }
  }

  for (const DocumentEntitySnapshot &child : snapshot.children) {
    const EntityId child_duplicate =
        document_command_duplicate_entity_snapshot_tree(
            scene, child, duplicate, include_modeling_object,
            existing_clones_by_source, name_allocator);
    if (child_duplicate == kInvalidEntityId) {
      return kInvalidEntityId;
    }
  }
  return duplicate;
}

inline EntityId document_command_restore_entity_snapshot_tree(
    SceneDocument &scene, const DocumentEntitySnapshot &snapshot,
    EntityId parent, std::unordered_map<EntityId, EntityId> &remapped_entities,
    bool include_modeling_object = true) {
  const EntityId restored = document_command_create_entity_from_snapshot(
      scene, snapshot, parent, snapshot.name, include_modeling_object, true);
  if (restored == kInvalidEntityId) {
    return kInvalidEntityId;
  }
  remapped_entities[snapshot.original_id] = restored;
  for (const DocumentEntitySnapshot &child : snapshot.children) {
    if (document_command_restore_entity_snapshot_tree(
            scene, child, restored, remapped_entities,
            include_modeling_object) == kInvalidEntityId) {
      return kInvalidEntityId;
    }
  }
  return restored;
}

inline std::vector<EntityId>
document_command_selected_root_entities(const SceneDocument &scene) {
  const std::vector<EntityId> selected = scene.selected_entities();
  std::unordered_set<EntityId> selected_set(selected.begin(), selected.end());
  std::vector<EntityId> roots;
  for (const EntityId entity : scene.entity_tree_order()) {
    if (!selected_set.contains(entity)) {
      continue;
    }
    if (scene.is_scene_root(entity)) {
      continue;
    }

    bool ancestor_selected = false;
    EntityId parent = scene.parent(entity);
    while (parent != kInvalidEntityId) {
      if (selected_set.contains(parent)) {
        ancestor_selected = true;
        break;
      }
      parent = scene.parent(parent);
    }
    if (!ancestor_selected) {
      roots.push_back(entity);
    }
  }
  return roots;
}

inline EntityId
document_command_common_parent(const SceneDocument &scene,
                               const std::vector<EntityId> &entities) {
  if (entities.empty()) {
    return kInvalidEntityId;
  }

  const EntityId parent = scene.parent(entities.front());
  for (const EntityId entity : entities) {
    if (scene.parent(entity) != parent) {
      return kInvalidEntityId;
    }
  }
  return parent;
}

inline void
document_command_select_only(SceneDocument &scene,
                             const std::vector<EntityId> &entities) {
  document_command_apply_selection(
      scene, document_command_selection_for(scene, entities));
}

inline std::vector<DocumentEntityPlacement>
document_command_capture_placements(const SceneDocument &scene,
                                    const std::vector<EntityId> &entities) {
  std::vector<DocumentEntityPlacement> placements;
  placements.reserve(entities.size());
  for (const EntityId entity : entities) {
    if (!scene.exists(entity)) {
      continue;
    }
    const TransformComponent *transform = scene.transform(entity);
    placements.push_back(DocumentEntityPlacement{
        .entity = entity,
        .parent = scene.parent(entity),
        .transform = transform == nullptr ? TransformComponent{} : *transform,
    });
  }
  return placements;
}

inline DocumentCommandResult
document_command_apply_placement(SceneDocument &scene,
                                 const DocumentEntityPlacement &placement) {
  if (!scene.exists(placement.entity)) {
    return document_command_missing_entity_result();
  }
  if (placement.parent != kInvalidEntityId && !scene.exists(placement.parent)) {
    return document_command_invalid_request_result(
        "Parent entity does not exist");
  }
  if (!scene.set_parent(placement.entity, placement.parent, false)) {
    return document_command_invalid_request_result(
        "Entity cannot be reparented");
  }
  scene.set_transform(placement.entity, placement.transform);
  return document_command_changed_result();
}

} // namespace quader

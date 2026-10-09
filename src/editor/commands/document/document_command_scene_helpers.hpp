////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/release_features.hpp"
#include "core/types.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "scene/document/scene_entity_state.hpp"
#include "scene/scene_entity_naming.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

using DocumentSelectionState = SceneSelectionState;

using DocumentEntitySnapshot = SceneEntityStateTree;

/// Rejects duplication of unpublished entity types before preview/history work.
[[nodiscard]] inline bool document_command_hierarchy_available_in_release(
    const SceneDocument &scene, std::span<const EntityId> roots) {
  const bool lights_available = release_has_feature(ReleaseFeature::Lights);
  const bool curves_available = release_has_feature(ReleaseFeature::Curves);
  const bool layers_available = release_has_feature(ReleaseFeature::Layers);
  const bool instances_available = release_has_feature(ReleaseFeature::Instancing);
  if (lights_available && curves_available && layers_available && instances_available) {
    return true;
  }
  for (const EntityId root : roots) {
    if (!scene.exists(root)) {
      continue;
    }
    for (const EntityId entity : scene.descendants(root, true)) {
      if ((!lights_available &&
           (scene.kind(entity) == EntityKind::Light || scene.light(entity))) ||
          (!curves_available &&
           (scene.kind(entity) == EntityKind::Curve || scene.curve(entity))) ||
          (!layers_available && scene.entity_layer(entity) != kInvalidSceneLayerId) ||
          (!instances_available &&
           (scene.kind(entity) == EntityKind::Instance ||
            scene.instance_definition(entity) != nullptr ||
            scene.instance_occurrence(entity) != nullptr))) {
        return false;
      }
    }
  }
  return true;
}

/**
 * Represents a Document Entity Placement value used by the editor document command layer.
 */
struct DocumentEntityPlacement {
  EntityId entity = kInvalidEntityId;
  EntityId parent = kInvalidEntityId;
  TransformComponent transform;
  SceneLayerId layer_id = kInvalidSceneLayerId;
  std::optional<std::size_t> sibling_ordinal;
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
  std::unordered_set<EntityId> seen;
  std::erase_if(result, [&seen](EntityId entity) {
    return !seen.insert(entity).second;
  });
  return result;
}

inline DocumentSelectionState
document_command_capture_selection(const SceneDocument &scene) {
  return capture_scene_selection(scene);
}

inline DocumentSelectionState
document_command_selection_for(const SceneDocument &scene,
                               const std::vector<EntityId> &entities,
                               EntityId active = kInvalidEntityId) {
  return make_scene_selection(scene, entities, active);
}

inline bool
document_command_same_selection(const DocumentSelectionState &left,
                                const DocumentSelectionState &right) {
  return same_scene_selection(left, right);
}

inline void
document_command_apply_selection(SceneDocument &scene,
                                 const DocumentSelectionState &state) {
  (void)apply_scene_selection(
      scene, state, SceneSelectionApplicationMode::UndoableCommand);
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
  return capture_scene_entity_state_tree(scene, entity);
}

inline void document_command_apply_entity_snapshot_components(
    SceneDocument &scene, EntityId entity,
    const DocumentEntitySnapshot &snapshot, bool include_modeling_object) {
  (void)apply_scene_entity_state(
      scene, entity, snapshot,
      SceneEntityStateApplyOptions{
          .include_modeling_object = include_modeling_object,
      });
}

inline void document_command_apply_entity_snapshot_common_state(
    SceneDocument &scene, EntityId entity,
    const DocumentEntitySnapshot &snapshot, std::string name) {
  (void)apply_scene_entity_common_state(scene, entity, snapshot,
                                        std::move(name));
}

inline EntityId document_command_create_entity_from_snapshot(
    SceneDocument &scene, const SceneEntityState &snapshot,
    EntityId parent, std::string name, bool include_modeling_object,
    bool preserve_identity = false) {
  return create_scene_entity_from_state(
      scene, snapshot, parent, std::move(name),
      preserve_identity ? SceneEntityStateIdentityMode::Preserve
                        : SceneEntityStateIdentityMode::AllocateNew,
      SceneEntityStateApplyOptions{
          .include_modeling_object = include_modeling_object,
      });
}

/**
 * Tracks scene mutations made while recursively duplicating one snapshot tree
 * so a child failure can restore reused clones and destroy new clones.
 */
struct DocumentDuplicateSnapshotRollback final {
  /**
   * Captures the original state of one existing clone reused by a duplicate.
   */
  struct ReusedEntity {
    EntityId entity = kInvalidEntityId;
    DocumentEntitySnapshot before;
  };

  std::vector<EntityId> created;
  std::vector<ReusedEntity> reused;
};

inline bool document_command_restore_placement_order(
    SceneDocument &scene, std::span<const DocumentEntityPlacement> placements);

inline void document_command_rollback_duplicate_snapshot_tree(
    SceneDocument &scene, DocumentDuplicateSnapshotRollback &rollback) {
  std::vector<DocumentEntityPlacement> placements;
  for (auto it = rollback.reused.rbegin(); it != rollback.reused.rend(); ++it) {
    if (!scene.exists(it->entity)) {
      continue;
    }
    (void)scene.set_parent(it->entity, it->before.parent, false);
    placements.push_back({
        .entity = it->entity,
        .parent = it->before.parent,
        .sibling_ordinal = it->before.sibling_ordinal,
    });
    document_command_apply_entity_snapshot_common_state(
        scene, it->entity, it->before, it->before.name);
  }
  for (auto it = rollback.created.rbegin(); it != rollback.created.rend();
       ++it) {
    if (scene.exists(*it)) {
      (void)scene.destroy_entity(*it);
    }
  }
  (void)document_command_restore_placement_order(scene, placements);
}

inline EntityId document_command_duplicate_entity_snapshot_tree_impl(
    SceneDocument &scene, const DocumentEntitySnapshot &snapshot,
    EntityId parent, bool include_modeling_object = false,
    const std::unordered_map<EntityId, EntityId> *existing_clones_by_source =
        nullptr,
    SceneEntityNameAllocator *name_allocator = nullptr,
    DocumentDuplicateSnapshotRollback *rollback = nullptr) {
  EntityId duplicate = kInvalidEntityId;
  const std::string duplicate_name =
      name_allocator != nullptr
          ? name_allocator->next_name(snapshot.name)
          : SceneEntityNaming::next_name(scene, snapshot.name);
  if (existing_clones_by_source != nullptr) {
    const auto existing = existing_clones_by_source->find(snapshot.id);
    if (existing != existing_clones_by_source->end()) {
      duplicate = existing->second;
      if (duplicate == kInvalidEntityId || !scene.exists(duplicate)) {
        return kInvalidEntityId;
      }
      if (rollback != nullptr) {
        const std::optional<DocumentEntitySnapshot> before =
            document_command_capture_entity_snapshot(scene, duplicate);
        if (!before.has_value()) {
          return kInvalidEntityId;
        }
        rollback->reused.push_back({.entity = duplicate, .before = *before});
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
    if (rollback != nullptr) {
      rollback->created.push_back(duplicate);
    }
  }

  for (const DocumentEntitySnapshot &child : snapshot.children) {
    const EntityId child_duplicate =
        document_command_duplicate_entity_snapshot_tree_impl(
            scene, child, duplicate, include_modeling_object,
            existing_clones_by_source, name_allocator, rollback);
    if (child_duplicate == kInvalidEntityId) {
      return kInvalidEntityId;
    }
  }
  return duplicate;
}

inline EntityId document_command_duplicate_entity_snapshot_tree(
    SceneDocument &scene, const DocumentEntitySnapshot &snapshot,
    EntityId parent, bool include_modeling_object = false,
    const std::unordered_map<EntityId, EntityId> *existing_clones_by_source =
        nullptr,
    SceneEntityNameAllocator *name_allocator = nullptr) {
  DocumentDuplicateSnapshotRollback rollback;
  const EntityId duplicate = document_command_duplicate_entity_snapshot_tree_impl(
      scene, snapshot, parent, include_modeling_object,
      existing_clones_by_source, name_allocator, &rollback);
  if (duplicate == kInvalidEntityId) {
    document_command_rollback_duplicate_snapshot_tree(scene, rollback);
  }
  return duplicate;
}

inline void document_command_erase_snapshot_remaps(
    const DocumentEntitySnapshot &snapshot,
    std::unordered_map<EntityId, EntityId> &remapped_entities) {
  remapped_entities.erase(snapshot.id);
  for (const DocumentEntitySnapshot &child : snapshot.children) {
    document_command_erase_snapshot_remaps(child, remapped_entities);
  }
}

inline EntityId document_command_restore_entity_snapshot_tree(
    SceneDocument &scene, const DocumentEntitySnapshot &snapshot,
    EntityId parent, std::unordered_map<EntityId, EntityId> &remapped_entities,
    bool include_modeling_object = true, bool restore_root_ordinal = true) {
  SceneEntityState state = snapshot;
  if (!restore_root_ordinal) {
    state.sibling_ordinal.reset();
  }
  const EntityId restored = document_command_create_entity_from_snapshot(
      scene, state, parent, snapshot.name, include_modeling_object, true);
  if (restored == kInvalidEntityId) {
    return kInvalidEntityId;
  }
  remapped_entities[snapshot.id] = restored;
  for (const DocumentEntitySnapshot &child : snapshot.children) {
    if (document_command_restore_entity_snapshot_tree(
            scene, child, restored, remapped_entities,
            include_modeling_object) == kInvalidEntityId) {
      if (scene.exists(restored)) {
        (void)scene.destroy_entity(restored);
      }
      document_command_erase_snapshot_remaps(snapshot, remapped_entities);
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
    if (!scene.is_effectively_selectable(entity)) {
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
        .layer_id = scene.entity_layer(entity),
        .sibling_ordinal = scene.sibling_ordinal(entity),
    });
  }
  return placements;
}

/** Restores captured sibling slots after every participant has its final parent. */
inline bool document_command_restore_placement_order(
    SceneDocument &scene, std::span<const DocumentEntityPlacement> placements) {
  std::map<EntityId, std::vector<const DocumentEntityPlacement *>> by_parent;
  for (const DocumentEntityPlacement &placement : placements) {
    if (!scene.exists(placement.entity) ||
        scene.parent(placement.entity) != placement.parent) {
      return false;
    }
    if (placement.sibling_ordinal.has_value()) {
      by_parent[placement.parent].push_back(&placement);
    }
  }
  std::map<EntityId, std::size_t> sibling_counts;
  for (auto &[parent, positioned] : by_parent) {
    const std::size_t count = scene.children(parent).size();
    sibling_counts.emplace(parent, count);
    std::unordered_set<EntityId> restored;
    std::unordered_set<std::size_t> ordinals;
    for (const DocumentEntityPlacement *placement : positioned) {
      const std::size_t ordinal = *placement->sibling_ordinal;
      if (ordinal >= count || !ordinals.insert(ordinal).second ||
          !restored.insert(placement->entity).second) {
        return false;
      }
    }
    std::ranges::sort(positioned, [](const auto *left, const auto *right) {
      return left->sibling_ordinal < right->sibling_ordinal;
    });
  }
  // Move only captured participants. Touching untouched siblings would advance
  // their mutation counters and invalidate otherwise unrelated checkpoints.
  for (const auto &[parent, positioned] : by_parent) {
    for (const DocumentEntityPlacement *placement : positioned) {
      if (!scene.set_sibling_ordinal(placement->entity,
                                     sibling_counts.at(parent) - 1u)) {
        return false;
      }
    }
    for (const DocumentEntityPlacement *placement : positioned) {
      if (!scene.set_sibling_ordinal(placement->entity,
                                     *placement->sibling_ordinal)) {
        return false;
      }
    }
  }
  return true;
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
  if (!scene.set_entity_layer(placement.entity, placement.layer_id)) {
    return document_command_invalid_request_result(
        "Entity layer membership could not be restored");
  }
  return document_command_changed_result();
}

} // namespace quader

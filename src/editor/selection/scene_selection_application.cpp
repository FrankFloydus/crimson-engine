////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/scene_selection_application.hpp"

#include "scene/components.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <cstddef>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace quader {
namespace {

std::vector<EntityId>
normalize_existing_scene_entities(const SceneDocument &scene,
                                  std::span<const EntityId> entities) {
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

EntityId resolve_active_selection(std::span<const EntityId> selected,
                                  EntityId requested) {
  if (requested != kInvalidEntityId &&
      std::find(selected.begin(), selected.end(), requested) !=
          selected.end()) {
    return requested;
  }
  return selected.empty() ? kInvalidEntityId : selected.back();
}

bool apply_undoable_scene_selection(SceneDocument &scene,
                                    const SceneSelectionState &state) {
  const std::uint64_t revision_before = scene.revision();
  (void)scene.replace_selection(state.selected, state.active);
  if (scene.revision() == revision_before && !state.selected.empty()) {
    scene.clear_selection();
    (void)scene.replace_selection(state.selected, state.active);
  }
  return scene.revision() != revision_before;
}

bool apply_runtime_sync_scene_selection(SceneDocument &scene,
                                        const SceneSelectionState &state) {
  const std::uint64_t revision_before = scene.revision();
  std::vector<EntityId> ordered_selected;
  ordered_selected.reserve(state.selected.size());
  std::unordered_set<EntityId> desired;
  desired.reserve(state.selected.size());
  for (const EntityId entity : state.selected) {
    if (scene.exists(entity) && desired.insert(entity).second) {
      ordered_selected.push_back(entity);
    }
  }
  const EntityId resolved_active = resolve_active_selection(
      std::span<const EntityId>(ordered_selected.data(),
                                ordered_selected.size()),
      state.active);
  (void)scene.replace_selection(ordered_selected, resolved_active);
  return scene.revision() != revision_before;
}

} // namespace

SceneSelectionState capture_scene_selection(const SceneDocument &scene) {
  return {
      .selected = scene.selected_entities(),
      .active = scene.primary_selection(),
  };
}

SceneSelectionState make_scene_selection(const SceneDocument &scene,
                                         std::span<const EntityId> entities,
                                         EntityId active) {
  EntityId resolved_active = active;
  if (resolved_active == kInvalidEntityId) {
    for (std::size_t index = entities.size(); index > 0; --index) {
      const EntityId entity = entities[index - 1u];
      if (entity != kInvalidEntityId && scene.exists(entity)) {
        resolved_active = entity;
        break;
      }
    }
  }

  std::vector<EntityId> selected =
      normalize_existing_scene_entities(scene, entities);
  if (std::find(selected.begin(), selected.end(), resolved_active) ==
      selected.end()) {
    resolved_active = selected.empty() ? kInvalidEntityId : selected.front();
  }
  return {
      .selected = std::move(selected),
      .active = resolved_active,
  };
}

bool same_scene_selection(const SceneSelectionState &left,
                          const SceneSelectionState &right) {
  return left.active == right.active && left.selected == right.selected;
}

bool scene_entity_selectable_for_user_selection(const SceneDocument &scene,
                                                EntityId entity) {
  if (entity == kInvalidEntityId || !scene.exists(entity)) {
    return false;
  }
  if (scene.is_scene_root(entity)) {
    return true;
  }
  if (scene.is_effectively_locked(entity)) {
    return false;
  }
  const VisibilityComponent *visibility = scene.visibility(entity);
  return visibility == nullptr || visibility->selectable;
}

std::vector<EntityId>
user_selectable_scene_entities(const SceneDocument &scene,
                               std::span<const EntityId> entities) {
  std::vector<EntityId> result;
  result.reserve(entities.size());
  std::unordered_set<EntityId> seen;
  seen.reserve(entities.size());
  for (const EntityId entity : entities) {
    if (!scene_entity_selectable_for_user_selection(scene, entity) ||
        !seen.insert(entity).second) {
      continue;
    }
    result.push_back(entity);
  }
  return result;
}

EntityId user_selectable_active_entity(const SceneDocument &scene,
                                       const std::vector<EntityId> &entities,
                                       EntityId active) {
  if (active != kInvalidEntityId &&
      scene_entity_selectable_for_user_selection(scene, active) &&
      std::find(entities.begin(), entities.end(), active) != entities.end()) {
    return active;
  }
  return entities.empty() ? kInvalidEntityId : entities.back();
}

SceneSelectionState make_user_scene_selection(
    const SceneDocument &scene, std::span<const EntityId> entities,
    EntityId active) {
  std::vector<EntityId> selectable =
      user_selectable_scene_entities(scene, entities);
  const EntityId selectable_active =
      user_selectable_active_entity(scene, selectable, active);
  return {
      .selected = std::move(selectable),
      .active = selectable_active,
  };
}

std::vector<EntityId>
user_selectable_scene_entities_for_mode(const SceneDocument &scene,
                                        SceneSelectionBulkMode mode) {
  std::vector<EntityId> result;
  std::unordered_map<EntityId, bool> effectively_locked;
  const std::vector<EntityId> tree_order = scene.entity_tree_order();
  result.reserve(tree_order.size());
  effectively_locked.reserve(tree_order.size());
  for (const EntityId entity : tree_order) {
    const EntityId parent = scene.parent(entity);
    bool locked =
        parent != kInvalidEntityId && effectively_locked[parent];
    if (const LockComponent *component = scene.lock(entity);
        component != nullptr && component->locked) {
      locked = true;
    }
    const SceneLayerId layer_id = scene.entity_layer(entity);
    if (layer_id != kInvalidSceneLayerId) {
      const SceneLayerRecord *layer = scene.layer(layer_id);
      if (layer == nullptr || layer->locked) {
        locked = true;
      }
    }
    effectively_locked.emplace(entity, locked);
    const VisibilityComponent *visibility = scene.visibility(entity);
    if (scene.is_scene_root(entity) || locked ||
        (visibility != nullptr && !visibility->selectable)) {
      continue;
    }
    if (mode == SceneSelectionBulkMode::Group &&
        scene.kind(entity) != EntityKind::Empty) {
      continue;
    }
    result.push_back(entity);
  }
  return result;
}

SceneSelectionState
resolve_scene_selection_edit(std::span<const EntityId> current_selection,
                             EntityId current_active,
                             std::span<const EntityId> edited_entities,
                             SceneSelectionEdit edit) {
  SceneSelectionState result;
  if (edit == SceneSelectionEdit::Replace) {
    result.selected.reserve(edited_entities.size());
    std::unordered_set<EntityId> seen;
    seen.reserve(edited_entities.size());
    for (const EntityId entity : edited_entities) {
      if (entity == kInvalidEntityId || !seen.insert(entity).second) {
        continue;
      }
      result.selected.push_back(entity);
      result.active = entity;
    }
    return result;
  }

  std::list<EntityId> ordered;
  std::unordered_map<EntityId, std::list<EntityId>::iterator> membership;
  membership.reserve(current_selection.size() + edited_entities.size());
  for (const EntityId entity : current_selection) {
    if (entity == kInvalidEntityId || membership.contains(entity)) {
      continue;
    }
    ordered.push_back(entity);
    membership.emplace(entity, std::prev(ordered.end()));
  }
  result.active = membership.contains(current_active)
                      ? current_active
                      : (ordered.empty() ? kInvalidEntityId : ordered.back());
  for (const EntityId entity : edited_entities) {
    const auto found = membership.find(entity);
    const bool already_selected = found != membership.end();
    if (edit == SceneSelectionEdit::Add ||
        (edit == SceneSelectionEdit::Toggle && !already_selected)) {
      if (entity != kInvalidEntityId && !already_selected) {
        ordered.push_back(entity);
        membership.emplace(entity, std::prev(ordered.end()));
        result.active = entity;
      }
    } else if ((edit == SceneSelectionEdit::Remove ||
                edit == SceneSelectionEdit::Toggle) &&
               already_selected) {
      ordered.erase(found->second);
      membership.erase(found);
      result.active =
          ordered.empty() ? kInvalidEntityId : ordered.back();
    }
  }
  result.selected.assign(ordered.begin(), ordered.end());
  return result;
}

bool apply_scene_selection(SceneDocument &scene,
                           const SceneSelectionState &state,
                           SceneSelectionApplicationMode mode) {
  switch (mode) {
  case SceneSelectionApplicationMode::UndoableCommand:
    return apply_undoable_scene_selection(scene, state);
  case SceneSelectionApplicationMode::RuntimeSync:
    return apply_runtime_sync_scene_selection(scene, state);
  }
  return false;
}

bool add_scene_selection(SceneDocument &scene,
                         std::span<const EntityId> selected,
                         EntityId active,
                         SceneSelectionApplicationMode mode) {
  const std::uint64_t revision_before = scene.revision();
  switch (mode) {
  case SceneSelectionApplicationMode::UndoableCommand:
  case SceneSelectionApplicationMode::RuntimeSync: {
    std::vector<EntityId> combined = scene.selected_entities();
    std::unordered_set<EntityId> membership(combined.begin(),
                                            combined.end());
    membership.reserve(combined.size() + selected.size());
    for (const EntityId entity : selected) {
      if (scene.exists(entity) && membership.insert(entity).second) {
        combined.push_back(entity);
      }
    }
    EntityId resolved_active = active;
    if (resolved_active == kInvalidEntityId ||
        !membership.contains(resolved_active)) {
      const EntityId current_active = scene.primary_selection();
      resolved_active =
          current_active != kInvalidEntityId &&
                  membership.contains(current_active)
              ? current_active
              : resolve_active_selection(
                    std::span<const EntityId>(combined.data(),
                                              combined.size()),
                    kInvalidEntityId);
    }
    (void)scene.replace_selection(combined, resolved_active);
    break;
  }
  }
  return scene.revision() != revision_before;
}

} // namespace quader

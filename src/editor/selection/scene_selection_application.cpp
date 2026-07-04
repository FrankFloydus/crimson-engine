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
  scene.clear_selection();
  for (const EntityId entity : state.selected) {
    if (scene.exists(entity)) {
      scene.set_selected(entity, true);
    }
  }
  if (state.active != kInvalidEntityId && scene.exists(state.active)) {
    scene.set_active_selection(state.active);
  }
  return scene.revision() != revision_before;
}

bool apply_runtime_sync_scene_selection(SceneDocument &scene,
                                        const SceneSelectionState &state) {
  const std::uint64_t revision_before = scene.revision();
  std::unordered_set<EntityId> desired;
  desired.reserve(state.selected.size());
  std::vector<EntityId> ordered_selected;
  ordered_selected.reserve(state.selected.size());

  for (const EntityId entity : state.selected) {
    if (scene.exists(entity) && desired.insert(entity).second) {
      ordered_selected.push_back(entity);
    }
  }

  for (const EntityId entity : scene.selected_entities()) {
    if (!desired.contains(entity)) {
      scene.set_selected(entity, false);
    }
  }
  for (const EntityId entity : ordered_selected) {
    const SelectionComponent *selection = scene.selection(entity);
    if (selection == nullptr || !selection->selected) {
      scene.set_selected(entity, true);
    }
  }

  const EntityId resolved_active = resolve_active_selection(
      std::span<const EntityId>(ordered_selected.data(),
                                ordered_selected.size()),
      state.active);
  if (resolved_active != kInvalidEntityId) {
    scene.set_active_selection(resolved_active);
  }

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
  for (const EntityId entity : entities) {
    if (!scene_entity_selectable_for_user_selection(scene, entity) ||
        std::find(result.begin(), result.end(), entity) != result.end()) {
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
  for (const EntityId entity : scene.entity_tree_order()) {
    if (scene.is_scene_root(entity) ||
        !scene_entity_selectable_for_user_selection(scene, entity)) {
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
    for (const EntityId entity : edited_entities) {
      if (entity == kInvalidEntityId ||
          std::find(result.selected.begin(), result.selected.end(), entity) !=
              result.selected.end()) {
        continue;
      }
      result.selected.push_back(entity);
      result.active = entity;
    }
    return result;
  }

  result.selected.assign(current_selection.begin(), current_selection.end());
  result.active =
      std::find(result.selected.begin(), result.selected.end(),
                current_active) != result.selected.end()
          ? current_active
          : (result.selected.empty() ? kInvalidEntityId
                                     : result.selected.back());
  for (const EntityId entity : edited_entities) {
    const auto found =
        std::find(result.selected.begin(), result.selected.end(), entity);
    const bool already_selected = found != result.selected.end();
    if (edit == SceneSelectionEdit::Add ||
        (edit == SceneSelectionEdit::Toggle && !already_selected)) {
      if (entity != kInvalidEntityId && !already_selected) {
        result.selected.push_back(entity);
        result.active = entity;
      }
    } else if ((edit == SceneSelectionEdit::Remove ||
                edit == SceneSelectionEdit::Toggle) &&
               already_selected) {
      result.selected.erase(found);
      result.active =
          result.selected.empty() ? kInvalidEntityId : result.selected.back();
    }
  }
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
  case SceneSelectionApplicationMode::RuntimeSync:
    for (const EntityId entity : selected) {
      if (scene.exists(entity)) {
        scene.set_selected(entity, true);
      }
    }
    if (active != kInvalidEntityId && scene.exists(active)) {
      scene.set_active_selection(active);
    }
    break;
  }
  return scene.revision() != revision_before;
}

} // namespace quader

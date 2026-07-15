////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

/**
 * Stores the Scene Aware Delete State data contract used by the modeling command layer.
 */
struct SceneAwareDeleteState {
    std::vector<DocumentEntitySnapshot> roots;
    std::vector<DocumentEntitySnapshot> orphan_definitions;
    DocumentSelectionState before_selection;
    ModelingSelectionMode before_mode = ModelingSelectionMode::Object;
    std::vector<int> modeling_object_ids;
    std::vector<EntityId> current_roots;
    std::vector<EntityId> current_definition_roots;
    ModelingSessionMemento before_modeling;
    ModelingSessionMemento after_modeling;
    bool captured = false;
    bool runtime_delete_executed = false;
};

CommandResult to_command_result(ModelingCommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
}

ModelingCommandResult missing_runtime_result() {
  return {
      .ok = false,
      .changed = false,
      .message = "Scene-aware delete runtime is unavailable.",
  };
}

void append_modeling_object_ids(const DocumentEntitySnapshot &snapshot,
                                std::vector<int> &object_ids) {
  if (snapshot.modeling_object.has_value() &&
      snapshot.modeling_object->object_id != kInvalidModelingObjectId) {
    object_ids.push_back(snapshot.modeling_object->object_id);
  }
  for (const DocumentEntitySnapshot &child : snapshot.children) {
    append_modeling_object_ids(child, object_ids);
  }
}

std::vector<int> snapshot_modeling_object_ids(
    const std::vector<DocumentEntitySnapshot> &snapshots) {
  std::vector<int> object_ids;
  for (const DocumentEntitySnapshot &snapshot : snapshots) {
    append_modeling_object_ids(snapshot, object_ids);
  }
  std::sort(object_ids.begin(), object_ids.end());
  object_ids.erase(std::unique(object_ids.begin(), object_ids.end()),
                   object_ids.end());
  return object_ids;
}

EntityId
entity_for_snapshot_modeling_object(const DocumentEntitySnapshot &snapshot,
                                    const SceneAwareDeleteRuntime &runtime) {
  if (!snapshot.modeling_object.has_value()) {
    return kInvalidEntityId;
  }
  if (!runtime.entity_for_modeling_object) {
    return kInvalidEntityId;
  }

  const std::optional<EntityId> entity =
      runtime.entity_for_modeling_object(snapshot.modeling_object->object_id);
  return entity.value_or(kInvalidEntityId);
}

EntityId restore_scene_delete_snapshot(
    SceneDocument &scene, const SceneAwareDeleteRuntime &runtime,
    const DocumentEntitySnapshot &snapshot, EntityId parent,
    std::unordered_map<EntityId, EntityId> &remapped_entities) {
  EntityId restored = entity_for_snapshot_modeling_object(snapshot, runtime);
  if (restored == kInvalidEntityId || !scene.exists(restored)) {
    restored = document_command_create_entity_from_snapshot(
        scene, snapshot, kInvalidEntityId, snapshot.name, true, true);
  } else {
    (void)scene.rename_entity(restored, snapshot.name);
    (void)scene.set_editor_object_id(restored, snapshot.editor_object_id);
    document_command_apply_entity_snapshot_components(scene, restored, snapshot,
                                                      true);
  }

  if (restored == kInvalidEntityId || !scene.exists(restored)) {
    return kInvalidEntityId;
  }
  if (!scene.set_parent(restored, parent, false)) {
    return kInvalidEntityId;
  }
  scene.set_transform(restored, snapshot.transform);
  remapped_entities[snapshot.original_id] = restored;

  for (const DocumentEntitySnapshot &child : snapshot.children) {
    if (restore_scene_delete_snapshot(scene, runtime, child, restored,
                                      remapped_entities) == kInvalidEntityId) {
      return kInvalidEntityId;
    }
  }
  return restored;
}

/**
 * Applies Scene Aware Delete Selection behavior as an undoable modeling command.
 */
class SceneAwareDeleteSelectionCommand final : public IUndoableCommand {
public:
    SceneAwareDeleteSelectionCommand(SceneDocument& scene, SceneAwareDeleteRuntime runtime)
        : scene_(scene)
        , runtime_(std::move(runtime)) {}

    [[nodiscard]] const std::string& label() const override {
        return label_;
    }

    CommandResult undo() override {
        if (!executed_) {
          return command_invalid_result(
              "Delete Selection has not been executed.");
        }
        return to_command_result(undo_scene_aware_delete());
    }

    CommandResult redo() override {
      ModelingCommandResult result = redo_scene_aware_delete(!executed_);
      if (result.ok && result.changed) {
        executed_ = true;
      }
      return to_command_result(std::move(result));
    }

private:
  ModelingCommandResult capture_scene_aware_delete_state() {
    if (!runtime_.app_selection_mode || !runtime_.capture_modeling_memento) {
      return missing_runtime_result();
    }

    state_ = {};
    state_.before_mode = runtime_.app_selection_mode();
    state_.before_selection = document_command_capture_selection(scene_);

    const std::vector<EntityId> roots =
        document_command_selected_root_entities(scene_);
    if (roots.empty()) {
      return {.ok = false, .changed = false, .message = "Selection is empty."};
    }

    state_.roots.reserve(roots.size());
    std::unordered_set<EntityId> deleted_entities;
    for (const EntityId root : roots) {
      std::optional<DocumentEntitySnapshot> snapshot =
          document_command_capture_entity_snapshot(scene_, root);
      if (!snapshot.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Selected entity is missing."};
      }
      state_.roots.push_back(std::move(*snapshot));
      state_.current_roots.push_back(root);
      const std::vector<EntityId> descendants = scene_.descendants(root, true);
      deleted_entities.insert(descendants.begin(), descendants.end());
    }

    std::unordered_set<EntityId> affected_definitions;
    for (const EntityId deleted : deleted_entities) {
      const EntityId definition =
          scene_.containing_instance_definition(deleted);
      if (definition != kInvalidEntityId && definition != deleted) {
        affected_definitions.insert(definition);
      }
    }
    for (const EntityId definition : affected_definitions) {
      const std::vector<EntityId> top_level_members =
          scene_.children(definition);
      if (!top_level_members.empty() &&
          std::ranges::all_of(top_level_members, [&](EntityId member) {
            return deleted_entities.contains(member);
          })) {
        return {
            .ok = false,
            .changed = false,
            .message =
                "An instance definition must retain at least one member.",
        };
      }
    }

    std::unordered_set<EntityId> captured_definitions;
    for (const EntityId deleted : deleted_entities) {
      const InstanceOccurrenceComponent *occurrence =
          scene_.instance_occurrence(deleted);
      if (occurrence == nullptr ||
          !captured_definitions.insert(occurrence->definition).second) {
        continue;
      }
      const std::vector<EntityId> occurrences =
          scene_.instance_occurrences_for_definition(occurrence->definition);
      const bool survives = std::ranges::any_of(
          occurrences, [&](EntityId candidate) {
            return !deleted_entities.contains(candidate);
          });
      if (survives) {
        continue;
      }
      std::optional<DocumentEntitySnapshot> definition_snapshot =
          document_command_capture_entity_snapshot(scene_,
                                                   occurrence->definition);
      if (!definition_snapshot.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Instance definition is missing."};
      }
      state_.orphan_definitions.push_back(std::move(*definition_snapshot));
      state_.current_definition_roots.push_back(occurrence->definition);
    }
    state_.modeling_object_ids = snapshot_modeling_object_ids(state_.roots);
    const std::vector<int> orphan_modeling_ids =
        snapshot_modeling_object_ids(state_.orphan_definitions);
    state_.modeling_object_ids.insert(state_.modeling_object_ids.end(),
                                      orphan_modeling_ids.begin(),
                                      orphan_modeling_ids.end());
    std::ranges::sort(state_.modeling_object_ids);
    state_.modeling_object_ids.erase(
        std::unique(state_.modeling_object_ids.begin(),
                    state_.modeling_object_ids.end()),
        state_.modeling_object_ids.end());
    state_.before_modeling = runtime_.capture_modeling_memento();
    state_.captured = true;
    return {.ok = true, .changed = !state_.roots.empty()};
  }

  ModelingCommandResult execute_scene_aware_delete_modeling_objects() {
    if (state_.modeling_object_ids.empty()) {
      return {.ok = true, .changed = false};
    }
    if (!runtime_.delete_modeling_objects) {
      return missing_runtime_result();
    }
    ModelingCommandResult result =
        runtime_.delete_modeling_objects(state_.modeling_object_ids);
    if (result.ok && result.changed) {
      state_.runtime_delete_executed = true;
    }
    return result;
  }

  ModelingCommandResult destroy_scene_aware_delete_roots() {
    std::vector<EntityId> destroyed = state_.current_roots;
    destroyed.insert(destroyed.end(), state_.current_definition_roots.begin(),
                     state_.current_definition_roots.end());
    const bool changed = scene_.destroy_entities(destroyed);
    state_.current_roots.clear();
    state_.current_definition_roots.clear();
    invalidate_selection_overlay_caches();
    return {.ok = true, .changed = changed};
  }

  ModelingCommandResult redo_scene_aware_delete(bool initial_redo) {
    if (initial_redo) {
      ModelingCommandResult captured = capture_scene_aware_delete_state();
      if (!captured.ok || !captured.changed) {
        return captured;
      }
      ModelingCommandResult runtime_delete =
          execute_scene_aware_delete_modeling_objects();
      if (!runtime_delete.ok) {
        return runtime_delete;
      }
      ModelingCommandResult scene_delete = destroy_scene_aware_delete_roots();
      if (runtime_delete.changed) {
        if (!runtime_.capture_modeling_memento) {
          return missing_runtime_result();
        }
        state_.after_modeling = runtime_.capture_modeling_memento();
      }
      return {
          .ok = true,
          .changed = runtime_delete.changed || scene_delete.changed,
          .message = "Delete Selection",
      };
    }

    ModelingCommandResult runtime_redo{.ok = true, .changed = false};
    if (state_.runtime_delete_executed) {
      if (!runtime_.restore_modeling_memento) {
        return missing_runtime_result();
      }
      runtime_redo = runtime_.restore_modeling_memento(state_.after_modeling);
      if (!runtime_redo.ok) {
        return runtime_redo;
      }
    }
    ModelingCommandResult scene_delete = destroy_scene_aware_delete_roots();
    return {
        .ok = true,
        .changed = runtime_redo.changed || scene_delete.changed,
        .message = "Delete Selection",
    };
  }

  ModelingCommandResult undo_scene_aware_delete() {
    if (!state_.captured) {
      return {.ok = false,
              .changed = false,
              .message = "Delete selection was not captured."};
    }

    if (state_.runtime_delete_executed) {
      if (!runtime_.restore_modeling_memento) {
        return missing_runtime_result();
      }
      ModelingCommandResult runtime_undo =
          runtime_.restore_modeling_memento(state_.before_modeling);
      if (!runtime_undo.ok) {
        return runtime_undo;
      }
    }

    std::unordered_map<EntityId, EntityId> remapped_entities;
    std::vector<EntityId> restored_definitions;
    restored_definitions.reserve(state_.orphan_definitions.size());
    for (const DocumentEntitySnapshot &snapshot :
         state_.orphan_definitions) {
      const EntityId parent = snapshot.original_parent != kInvalidEntityId &&
                                      scene_.exists(snapshot.original_parent)
                                  ? snapshot.original_parent
                                  : kInvalidEntityId;
      const EntityId restored = restore_scene_delete_snapshot(
          scene_, runtime_, snapshot, parent, remapped_entities);
      if (restored == kInvalidEntityId) {
        return {.ok = false,
                .changed = false,
                .message = "Instance definition could not be restored."};
      }
      restored_definitions.push_back(restored);
    }
    std::vector<EntityId> restored_roots;
    restored_roots.reserve(state_.roots.size());
    for (const DocumentEntitySnapshot &snapshot : state_.roots) {
      const EntityId parent = snapshot.original_parent != kInvalidEntityId &&
                                      scene_.exists(snapshot.original_parent)
                                  ? snapshot.original_parent
                                  : kInvalidEntityId;
      const EntityId restored = restore_scene_delete_snapshot(
          scene_, runtime_, snapshot, parent, remapped_entities);
      if (restored == kInvalidEntityId) {
        return {.ok = false,
                .changed = false,
                .message = "Deleted hierarchy could not be restored."};
      }
      restored_roots.push_back(restored);
    }

    state_.current_roots = std::move(restored_roots);
    state_.current_definition_roots = std::move(restored_definitions);
    if (runtime_.set_app_selection_mode) {
      runtime_.set_app_selection_mode(state_.before_mode);
    }
    document_command_apply_selection(
        scene_, document_command_remap_selection(state_.before_selection,
                                                 remapped_entities));
    if (state_.before_mode == ModelingSelectionMode::Object) {
      if (!runtime_.sync_object_selection_from_scene) {
        return missing_runtime_result();
      }
      runtime_.sync_object_selection_from_scene(true);
    } else if (state_.before_mode == ModelingSelectionMode::Group) {
      if (!runtime_.clear_runtime_object_selection_for_group_mode) {
        return missing_runtime_result();
      }
      runtime_.clear_runtime_object_selection_for_group_mode();
    }
    invalidate_selection_overlay_caches();
    return {.ok = true, .changed = true, .message = "Delete Selection"};
  }

  void invalidate_selection_overlay_caches() const {
    if (runtime_.invalidate_selection_overlay_caches) {
      runtime_.invalidate_selection_overlay_caches();
    }
  }

    SceneDocument& scene_;
    SceneAwareDeleteRuntime runtime_;
    SceneAwareDeleteState state_;
    bool executed_ = false;
    std::string label_ = "Delete Selection";
};

} // namespace

bool scene_aware_delete_has_selected_root(const SceneDocument &scene) {
  return !document_command_selected_root_entities(scene).empty();
}

bool scene_aware_delete_uses_document_route(ModelingSelectionMode mode) {
  return selection_mode_uses_scene_document(mode);
}

bool scene_aware_delete_handles_selection(ModelingSelectionSummary summary,
                                          bool has_selected_root) {
  const bool component_selection =
      summary.has_selection &&
      !scene_aware_delete_uses_document_route(summary.mode);
  return has_selected_root && !component_selection;
}

std::unique_ptr<IUndoableCommand>
create_scene_aware_delete_selection_command(SceneDocument &scene,
                                            SceneAwareDeleteRuntime runtime) {
  return std::make_unique<SceneAwareDeleteSelectionCommand>(scene,
                                                            std::move(runtime));
}

} // namespace quader

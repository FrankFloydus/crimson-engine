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
#include <iterator>
#include <optional>
#include <span>
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

/** Captures one exact stable side of a delete history transition. */
struct SceneAwareDeleteTransitionCheckpoint {
  SceneModelingProjectionCheckpoint scene;
  std::vector<EntityId> entities;
  std::vector<EntityId> current_roots;
  std::vector<EntityId> current_definition_roots;
  ModelingSessionMemento modeling;
  ModelingSelectionMode mode = ModelingSelectionMode::Object;
};

/** Defers captured hierarchy placement until the entire deleted forest exists. */
struct SceneDeleteRestoredPlacement {
  EntityId entity = kInvalidEntityId;
  EntityId parent = kInvalidEntityId;
  const SceneEntityState *state = nullptr;
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
    std::unordered_map<EntityId, EntityId> &remapped_entities,
    std::vector<SceneDeleteRestoredPlacement> &placements) {
  EntityId restored = entity_for_snapshot_modeling_object(snapshot, runtime);
  if (restored == kInvalidEntityId || !scene.exists(restored)) {
    // The outer transition owns compensation. Keep a failed provisional
    // entity alive so its allocation receipt remains available to rollback.
    restored = scene.create_entity_with_id(snapshot.id, snapshot.name,
                                            snapshot.kind,
                                            snapshot.editor_object_id);
    if (restored == kInvalidEntityId ||
        (!snapshot.uuid.empty() &&
         !scene.set_entity_uuid(restored, snapshot.uuid))) {
      return kInvalidEntityId;
    }
  }
  if (!scene.rename_entity(restored, snapshot.name) ||
      !scene.set_editor_object_id(restored, snapshot.editor_object_id) ||
      !apply_scene_entity_state(scene, restored, snapshot)) {
    return kInvalidEntityId;
  }
  remapped_entities[snapshot.id] = restored;
  placements.push_back({restored, parent, &snapshot});

  for (const DocumentEntitySnapshot &child : snapshot.children) {
    if (restore_scene_delete_snapshot(scene, runtime, child, restored,
                                      remapped_entities, placements) ==
        kInvalidEntityId) {
      return kInvalidEntityId;
    }
  }
  return restored;
}

bool restore_scene_delete_placements(
    SceneDocument &scene,
    std::span<const SceneDeleteRestoredPlacement> placements) {
  std::vector<DocumentEntityPlacement> ordered;
  ordered.reserve(placements.size());
  for (const SceneDeleteRestoredPlacement &placement : placements) {
    if (scene.parent(placement.entity) != placement.parent &&
        !scene.set_parent(placement.entity, placement.parent, false)) {
      return false;
    }
    ordered.push_back({
        .entity = placement.entity,
        .parent = placement.parent,
        .sibling_ordinal = placement.state->sibling_ordinal,
    });
  }
  // Definitions and modeled descendants can be recreated in a different order.
  // Restore the complete forest before placing captured roots and children.
  return document_command_restore_placement_order(scene, ordered);
}

/**
 * Applies Scene Aware Delete Selection behavior as an undoable modeling command.
 */
class SceneAwareDeleteSelectionCommand final : public IUndoableCommand {
public:
    SceneAwareDeleteSelectionCommand(SceneDocument& scene,
                                     SceneAwareDeleteRuntime runtime,
                                     std::vector<EntityId> roots,
                                     std::string label)
        : scene_(scene)
        , runtime_(std::move(runtime))
        , requested_roots_(std::move(roots))
        , label_(std::move(label)) {}

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

    const std::vector<EntityId> &roots = requested_roots_;
    if (roots.empty()) {
      return {.ok = false,
              .changed = false,
              .message = "No entity hierarchy was provided."};
    }

    state_.roots.reserve(roots.size());
    std::unordered_set<EntityId> deleted_entities;
    for (const EntityId root : roots) {
      if (root == kInvalidEntityId || !scene_.exists(root)) {
        return {.ok = false,
                .changed = false,
                .message = "Selected entity is missing."};
      }
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

    std::unordered_set<EntityId> examined_definitions;
    std::unordered_set<EntityId> orphan_definitions;
    for (const EntityId deleted : deleted_entities) {
      const InstanceOccurrenceComponent *occurrence =
          scene_.instance_occurrence(deleted);
      if (occurrence == nullptr ||
          !examined_definitions.insert(occurrence->definition).second) {
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
      orphan_definitions.insert(occurrence->definition);
    }
    for (const EntityId definition : scene_.entity_tree_order()) {
      if (!orphan_definitions.contains(definition)) {
        continue;
      }
      std::optional<DocumentEntitySnapshot> definition_snapshot =
          document_command_capture_entity_snapshot(scene_, definition);
      if (!definition_snapshot.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Instance definition is missing."};
      }
      state_.orphan_definitions.push_back(std::move(*definition_snapshot));
      state_.current_definition_roots.push_back(definition);
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
    if (result.ok && !result.changed) {
      return {
          .ok = false,
          .changed = false,
          .message = "Selected modeling objects were not deleted.",
      };
    }
    if (result.ok) {
      state_.runtime_delete_executed = true;
    }
    return result;
  }

  ModelingCommandResult destroy_scene_aware_delete_roots() {
    std::vector<EntityId> destroyed = state_.current_roots;
    destroyed.insert(destroyed.end(), state_.current_definition_roots.begin(),
                     state_.current_definition_roots.end());
    std::ranges::sort(destroyed);
    destroyed.erase(std::unique(destroyed.begin(), destroyed.end()),
                    destroyed.end());
    std::vector<EntityId> existing;
    std::ranges::copy_if(destroyed, std::back_inserter(existing),
                         [&](EntityId entity) { return scene_.exists(entity); });
    if (existing.empty()) {
      return {
          .ok = false,
          .changed = false,
          .message = "Deleted hierarchy is already missing.",
      };
    }

    const bool reported = runtime_.destroy_scene_entities
                              ? runtime_.destroy_scene_entities(existing)
                              : scene_.destroy_entities(existing);
    const std::size_t remaining = static_cast<std::size_t>(std::ranges::count_if(
        existing, [&](EntityId entity) { return scene_.exists(entity); }));
    if (!reported) {
      return {
          .ok = false,
          .changed = remaining != existing.size(),
          .message = "Deleted hierarchy could not be destroyed.",
      };
    }
    if (remaining != 0u) {
      return {
          .ok = false,
          .changed = remaining != existing.size(),
          .message = "Deleted hierarchy was only partially destroyed.",
      };
    }
    state_.current_roots.clear();
    state_.current_definition_roots.clear();
    invalidate_selection_overlay_caches();
    return {.ok = true, .changed = true};
  }

  std::optional<SceneAwareDeleteTransitionCheckpoint>
  capture_transition_checkpoint() const {
    if (!runtime_.capture_modeling_memento || !runtime_.app_selection_mode) {
      return std::nullopt;
    }
    SceneAwareDeleteTransitionCheckpoint checkpoint;
    checkpoint.entities = scene_.entity_tree_order();
    checkpoint.current_roots = state_.current_roots;
    checkpoint.current_definition_roots =
        state_.current_definition_roots;
    checkpoint.scene = scene_.capture_modeling_projection_checkpoint(
        checkpoint.entities,
        SceneModelingProjectionDomain::Structure |
            SceneModelingProjectionDomain::MeshBindings |
            SceneModelingProjectionDomain::CurveBindings |
            SceneModelingProjectionDomain::Selection);
    if (!checkpoint.scene.valid()) {
      return std::nullopt;
    }
    checkpoint.modeling = runtime_.capture_modeling_memento();
    checkpoint.mode = runtime_.app_selection_mode();
    return checkpoint;
  }

  [[nodiscard]] bool rollback_transition(
      const SceneAwareDeleteTransitionCheckpoint &checkpoint) {
    std::unordered_set<EntityId> known_entities(checkpoint.entities.begin(),
                                                checkpoint.entities.end());
    std::unordered_set<EntityId> recorded_entities;
    std::vector<EntityId> created_entities;
    const auto capture_created_entities = [&]() {
      std::unordered_set<EntityId> current_entities;
      for (const EntityId entity : scene_.entity_tree_order()) {
        current_entities.insert(entity);
        if (!known_entities.contains(entity) &&
            recorded_entities.insert(entity).second) {
          created_entities.push_back(entity);
        }
      }
      known_entities = std::move(current_entities);
    };
    // Inverse modeling publication can remove freshly restored mesh entities.
    // Their allocation receipts are still required by the scene checkpoint.
    capture_created_entities();

    if (checkpoint.modeling.valid()) {
      const auto &restore_modeling = runtime_.restore_modeling_memento_exact
                                         ? runtime_.restore_modeling_memento_exact
                                         : runtime_.restore_modeling_memento;
      if (!restore_modeling) {
        return false;
      }
      // A restored occurrence can prevent inverse modeling from removing the
      // last mesh in its definition. Detach only provisional occurrence roles;
      // keep their entities and allocation receipts for the scene checkpoint.
      for (const EntityId entity : created_entities) {
        if (scene_.instance_occurrence(entity) != nullptr &&
            !scene_.clear_instance_occurrence(entity)) {
          return false;
        }
      }
      const ModelingCommandResult modeling_restore =
          restore_modeling(checkpoint.modeling);
      if (!modeling_restore.ok) {
        return false;
      }
    }

    // The Scene checkpoint owns removal of every entity allocated by the
    // failed transition as well as the exact scalar rewind. Destroying those
    // entities manually would add inverse mutations before preflight, and a
    // second restore would attempt to consume the same allocation lease twice.
    // Restore modeling first, then let Scene consume its lease exactly once.
    // Failed redo compensation can recreate a checkpoint entity with its old
    // identity. Its new allocation also needs a receipt, even though the scene
    // checkpoint will restore that entity instead of removing it.
    capture_created_entities();
    if (!scene_.restore_modeling_projection_checkpoint(checkpoint.scene,
                                                       created_entities)) {
      return false;
    }
    if (runtime_.set_app_selection_mode) {
      runtime_.set_app_selection_mode(checkpoint.mode);
    }
    invalidate_selection_overlay_caches();
    return true;
  }

  ModelingCommandResult compensated_failure(
      ModelingCommandResult failure,
      const SceneAwareDeleteTransitionCheckpoint &checkpoint) {
    if (rollback_transition(checkpoint)) {
      failure.changed = false;
      state_.current_roots = checkpoint.current_roots;
      state_.current_definition_roots =
          checkpoint.current_definition_roots;
      return failure;
    }
    failure.changed = true;
    failure.message =
        failure.message.empty()
            ? "Delete Selection failed and its stable state could not be "
              "restored."
            : failure.message +
                  " The stable delete state could not be restored.";
    return failure;
  }

  ModelingCommandResult redo_scene_aware_delete(bool initial_redo) {
    if (initial_redo) {
      ModelingCommandResult captured = capture_scene_aware_delete_state();
      if (!captured.ok || !captured.changed) {
        return captured;
      }
      const std::optional<SceneAwareDeleteTransitionCheckpoint> checkpoint =
          capture_transition_checkpoint();
      if (!checkpoint.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Delete Selection checkpoint is unavailable."};
      }
      ModelingCommandResult runtime_delete =
          execute_scene_aware_delete_modeling_objects();
      if (!runtime_delete.ok) {
        return runtime_delete.changed
                   ? compensated_failure(std::move(runtime_delete),
                                         *checkpoint)
                   : runtime_delete;
      }
      ModelingCommandResult scene_delete = destroy_scene_aware_delete_roots();
      if (!scene_delete.ok) {
        return compensated_failure(std::move(scene_delete), *checkpoint);
      }
      if (runtime_delete.changed) {
        state_.after_modeling = runtime_.capture_modeling_memento();
      }
      return {
          .ok = true,
          .changed = runtime_delete.changed || scene_delete.changed,
          .message = "Delete Selection",
      };
    }

    const std::optional<SceneAwareDeleteTransitionCheckpoint> checkpoint =
        capture_transition_checkpoint();
    if (!checkpoint.has_value()) {
      return {.ok = false,
              .changed = false,
              .message = "Delete Selection checkpoint is unavailable."};
    }
    if (state_.runtime_delete_executed &&
        !runtime_.restore_modeling_memento) {
      return missing_runtime_result();
    }
    ModelingCommandResult scene_delete = destroy_scene_aware_delete_roots();
    if (!scene_delete.ok) {
      return compensated_failure(std::move(scene_delete), *checkpoint);
    }

    ModelingCommandResult runtime_redo{.ok = true, .changed = false};
    if (state_.runtime_delete_executed) {
      runtime_redo = runtime_.restore_modeling_memento(state_.after_modeling);
      if (!runtime_redo.ok) {
        return compensated_failure(std::move(runtime_redo), *checkpoint);
      }
    }
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

    const std::optional<SceneAwareDeleteTransitionCheckpoint> checkpoint =
        capture_transition_checkpoint();
    if (!checkpoint.has_value()) {
      return {.ok = false,
              .changed = false,
              .message = "Delete Selection checkpoint is unavailable."};
    }

    if (state_.runtime_delete_executed) {
      if (!runtime_.restore_modeling_memento) {
        return missing_runtime_result();
      }
      ModelingCommandResult runtime_undo =
          runtime_.restore_modeling_memento(state_.before_modeling);
      if (!runtime_undo.ok) {
        return runtime_undo.changed
                   ? compensated_failure(std::move(runtime_undo), *checkpoint)
                   : runtime_undo;
      }
    }

    std::unordered_map<EntityId, EntityId> remapped_entities;
    std::vector<SceneDeleteRestoredPlacement> placements;
    std::vector<EntityId> restored_definitions;
    restored_definitions.reserve(state_.orphan_definitions.size());
    for (const DocumentEntitySnapshot &snapshot :
         state_.orphan_definitions) {
      const EntityId parent = snapshot.parent != kInvalidEntityId &&
                                      scene_.exists(snapshot.parent)
                                  ? snapshot.parent
                                  : kInvalidEntityId;
      const EntityId restored = restore_scene_delete_snapshot(
          scene_, runtime_, snapshot, parent, remapped_entities, placements);
      if (restored == kInvalidEntityId) {
        return compensated_failure(
            {.ok = false,
             .changed = true,
             .message = "Instance definition could not be restored."},
            *checkpoint);
      }
      restored_definitions.push_back(restored);
    }
    std::vector<EntityId> restored_roots;
    restored_roots.reserve(state_.roots.size());
    for (const DocumentEntitySnapshot &snapshot : state_.roots) {
      const EntityId parent = snapshot.parent != kInvalidEntityId &&
                                      scene_.exists(snapshot.parent)
                                  ? snapshot.parent
                                  : kInvalidEntityId;
      const EntityId restored = restore_scene_delete_snapshot(
          scene_, runtime_, snapshot, parent, remapped_entities, placements);
      if (restored == kInvalidEntityId) {
        return compensated_failure(
            {.ok = false,
             .changed = true,
             .message = "Deleted hierarchy could not be restored."},
            *checkpoint);
      }
      restored_roots.push_back(restored);
    }

    if (!restore_scene_delete_placements(scene_, placements)) {
      return compensated_failure(
          {.ok = false,
           .changed = true,
           .message = "Deleted hierarchy placement could not be restored."},
          *checkpoint);
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
        return compensated_failure(missing_runtime_result(), *checkpoint);
      }
      const ModelingCommandResult synchronized =
          runtime_.sync_object_selection_from_scene(true);
      if (!synchronized.ok) {
        ModelingCommandResult failure = synchronized;
        if (failure.message.empty()) {
          failure.message =
              "Restored scene selection could not be published to modeling.";
        }
        failure.changed = true;
        return compensated_failure(std::move(failure), *checkpoint);
      }
    } else if (state_.before_mode == ModelingSelectionMode::Group) {
      if (!runtime_.clear_runtime_object_selection_for_group_mode) {
        return compensated_failure(missing_runtime_result(), *checkpoint);
      }
      const ModelingCommandResult cleared =
          runtime_.clear_runtime_object_selection_for_group_mode();
      if (!cleared.ok) {
        ModelingCommandResult failure = cleared;
        if (failure.message.empty()) {
          failure.message =
              "Restored Group selection could not be published to modeling.";
        }
        failure.changed = true;
        return compensated_failure(std::move(failure), *checkpoint);
      }
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
    std::vector<EntityId> requested_roots_;
    SceneAwareDeleteState state_;
    bool executed_ = false;
    std::string label_;
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
  return create_scene_aware_destroy_entities_command(
      scene, std::move(runtime), document_command_selected_root_entities(scene),
      "Delete Selection");
}

std::unique_ptr<IUndoableCommand> create_scene_aware_destroy_entities_command(
    SceneDocument &scene, SceneAwareDeleteRuntime runtime,
    std::vector<EntityId> roots, std::string label) {
  return std::make_unique<SceneAwareDeleteSelectionCommand>(
      scene, std::move(runtime), std::move(roots), std::move(label));
}

} // namespace quader

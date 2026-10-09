////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/scene_structural_lifecycle_service.hpp"

#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/commands/modeling_selection_memento_command.hpp"
#include "editor/modeling/services/modeling_hierarchy_duplicate_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

SceneStructuralDuplicateResult duplicate_failure(CommandResult result) {
  return SceneStructuralDuplicateResult(std::move(result));
}

SceneStructuralDuplicateBatchResult
duplicate_batch_failure(CommandResult result) {
  return SceneStructuralDuplicateBatchResult(std::move(result));
}

CommandResult duplicate_rollback_failure(
    CommandResult failure, const CommandResult &document_rollback,
    const ModelingCommandResult &modeling_rollback) {
  const bool rollback_ok = document_rollback.ok && modeling_rollback.ok;
  if (rollback_ok) {
    failure.ok = false;
    failure.changed = false;
    if (failure.message.empty()) {
      failure.message = "Structural duplicate failed.";
    }
    return failure;
  }

  std::string message = failure.message.empty()
                            ? "Structural duplicate failed."
                            : std::move(failure.message);
  if (!document_rollback.ok) {
    message += "; document rollback failed: ";
    message += document_rollback.message.empty()
                   ? "unknown document rollback failure"
                   : document_rollback.message;
  }
  if (!modeling_rollback.ok) {
    message += "; modeling rollback failed: ";
    message += modeling_rollback.message.empty()
                   ? "unknown modeling rollback failure"
                   : modeling_rollback.message;
  }
  return {
      .ok = false,
      .changed = true,
      .message = std::move(message),
  };
}

CommandResult remove_duplicate_roots(
    SceneDocument &scene, std::span<const EntityId> duplicate_roots) {
  std::vector<EntityId> live_roots;
  live_roots.reserve(duplicate_roots.size());
  for (const EntityId root : duplicate_roots) {
    if (root != kInvalidEntityId && scene.exists(root)) {
      live_roots.push_back(root);
    }
  }
  if (live_roots.empty()) {
    return command_unchanged_result();
  }
  (void)scene.destroy_entities(live_roots);
  const bool removed = std::ranges::none_of(
      live_roots, [&scene](EntityId root) { return scene.exists(root); });
  return removed
             ? command_changed_result()
             : command_invalid_result(
                   "Duplicated scene roots could not be removed.");
}

struct PreparedStructuralDuplicate final {
  SceneStructuralDuplicateBatchResult result;
  SceneScalarCompensationCheckpoint scalar_checkpoint;
  DocumentSelectionState selection_before;
  ModelingSessionMemento modeling_before;
  std::vector<EntityId> duplicate_roots;
  bool contains_modeling_content = false;
};

PreparedStructuralDuplicate prepare_structural_duplicate(
    SceneDocument &scene, ModelingSessionService &modeling,
    DocumentCommandService &document_commands, EditorTransaction &transaction,
    std::span<const SceneStructuralDuplicateBatch> batches,
    ModelingSelectionMode result_selection_mode, const std::string &label,
    bool independent_mesh_assets, bool contains_modeling_content,
    SceneEntityNamePolicy name_policy = SceneEntityNamePolicy::DuplicateFamily) {
  PreparedStructuralDuplicate prepared;
  if (document_commands.property_edit_active()) {
    prepared.result = duplicate_batch_failure(command_invalid_result(
        "Accept the active property edit before starting a duplicate transaction."));
    return prepared;
  }
  prepared.scalar_checkpoint =
      scene.capture_scalar_compensation_checkpoint();
  if (!prepared.scalar_checkpoint.valid()) {
    prepared.result = duplicate_batch_failure(command_invalid_result(
        "Scene scalar compensation scope is unavailable."));
    return prepared;
  }
  prepared.selection_before = document_command_capture_selection(scene);
  prepared.contains_modeling_content = contains_modeling_content;
  prepared.modeling_before =
      contains_modeling_content ? modeling.capture_history_memento()
                                : ModelingSessionMemento{};

  const auto preserve_after_scope_failure = [&](CommandResult failure) {
    (void)scene.discard_scalar_compensation_checkpoint(
        prepared.scalar_checkpoint);
    const CommandResult preserved =
        transaction.active() ? transaction.commit()
                             : command_unchanged_result();
    failure.ok = false;
    failure.changed = true;
    if (!failure.message.empty()) {
      failure.message += "; ";
    }
    failure.message +=
        "Structural duplicate rollback was rejected because the Scene "
        "changed outside the compensation scope.";
    if (!preserved.ok) {
      failure.message +=
          "; applied duplicate state could not be retained in history: ";
      failure.message += preserved.message;
    }
    prepared.result = duplicate_batch_failure(std::move(failure));
  };

  const auto cancel_and_restore = [&](CommandResult failure) {
    if (!scene.acknowledge_scalar_compensation_checkpoint(
            prepared.scalar_checkpoint) ||
        !scene.begin_scalar_compensation_restore(
            prepared.scalar_checkpoint)) {
      preserve_after_scope_failure(std::move(failure));
      return;
    }

    CommandResult document_rollback =
        transaction.active() ? transaction.cancel()
                             : command_unchanged_result();
    ModelingCommandResult modeling_rollback{
        .ok = true,
        .changed = false,
    };
    if (contains_modeling_content) {
      modeling_rollback =
          modeling.restore_history_memento_exact_for_failure_compensation(
              prepared.modeling_before);
    }

    if (!document_rollback.ok) {
      const CommandResult removed =
          remove_duplicate_roots(scene, prepared.duplicate_roots);
      CommandResult retried = command_unchanged_result();
      if (transaction.active()) {
        retried = transaction.cancel();
      }
      if (removed.ok && retried.ok) {
        document_rollback = command_changed_result();
      } else {
        std::string message = document_rollback.message.empty()
                                  ? "Transaction cancellation failed"
                                  : document_rollback.message;
        if (!removed.ok) {
          message += "; direct cleanup failed: ";
          message += removed.message;
        }
        if (!retried.ok) {
          message += "; cancellation retry failed: ";
          message += retried.message;
        }
        document_rollback = command_invalid_result(std::move(message));
      }
      if (transaction.active()) {
        (void)transaction.discard();
      }
    }

    document_command_apply_selection(scene, prepared.selection_before);
    const bool selection_restored = document_command_same_selection(
        prepared.selection_before, document_command_capture_selection(scene));
    CommandResult result = duplicate_rollback_failure(
        std::move(failure), document_rollback, modeling_rollback);
    if (!document_rollback.ok || !modeling_rollback.ok ||
        !selection_restored) {
      (void)scene.discard_scalar_compensation_checkpoint(
          prepared.scalar_checkpoint);
      result.ok = false;
      result.changed = true;
      if (!selection_restored) {
        if (!result.message.empty()) {
          result.message += "; ";
        }
        result.message +=
            "structural duplicate rollback did not restore the exact scene "
            "selection";
      }
    } else if (!scene.restore_scalar_compensation_checkpoint(
                   prepared.scalar_checkpoint)) {
      result.ok = false;
      result.changed = true;
      if (!result.message.empty()) {
        result.message += "; ";
      }
      result.message += "structural duplicate scalar compensation failed";
    }
    prepared.result = duplicate_batch_failure(std::move(result));
  };

  if (contains_modeling_content && !prepared.modeling_before.valid()) {
    cancel_and_restore(command_invalid_result(
        "Structural duplicate could not capture its modeling rollback "
        "state."));
    return prepared;
  }

  std::string duplicate_message;
  prepared.result.batches.reserve(batches.size());
  for (const SceneStructuralDuplicateBatch &batch : batches) {
    ModelingHierarchyDuplicateResult duplicated =
        modeling.duplicate_entity_hierarchy(
            batch.roots, document_commands, result_selection_mode,
            independent_mesh_assets, name_policy);
    prepared.duplicate_roots.insert(prepared.duplicate_roots.end(),
                                    duplicated.duplicate_roots.begin(),
                                    duplicated.duplicate_roots.end());

    if (!duplicated.ok || !duplicated.changed) {
      cancel_and_restore(CommandResult{
          .ok = duplicated.ok,
          .changed = duplicated.changed,
          .message = duplicated.message,
      });
      return prepared;
    }
    if (duplicated.source_roots.size() != duplicated.duplicate_roots.size()) {
      cancel_and_restore(command_invalid_result(
          "Hierarchy duplicate returned an incomplete root mapping."));
      return prepared;
    }
    if (duplicated.source_modeling_entities.size() !=
        duplicated.duplicate_modeling_entities.size()) {
      cancel_and_restore(command_invalid_result(
          "Hierarchy duplicate returned an incomplete modeling mapping."));
      return prepared;
    }

    SceneStructuralDuplicateResult batch_result(CommandResult{
        .ok = true,
        .changed = true,
        .message = duplicated.message,
    });
    batch_result.clones.reserve(duplicated.source_roots.size());
    for (std::size_t index = 0; index < duplicated.source_roots.size();
         ++index) {
      batch_result.clones.push_back({
          .source = duplicated.source_roots[index],
          .duplicate = duplicated.duplicate_roots[index],
      });
    }
    batch_result.modeling_clones.reserve(
        duplicated.source_modeling_entities.size());
    for (std::size_t index = 0;
         index < duplicated.source_modeling_entities.size(); ++index) {
      const EntityId source = duplicated.source_modeling_entities[index];
      const EntityId duplicate = duplicated.duplicate_modeling_entities[index];
      const ModelingObjectComponent *source_modeling =
          scene.modeling_object(source);
      const ModelingObjectComponent *duplicate_modeling =
          scene.modeling_object(duplicate);
      if (source_modeling == nullptr || duplicate_modeling == nullptr ||
          source_modeling->object_id == kInvalidModelingObjectId ||
          duplicate_modeling->object_id == kInvalidModelingObjectId) {
        cancel_and_restore(command_invalid_result(
            "Hierarchy duplicate returned an incomplete native modeling "
            "identity."));
        return prepared;
      }
      batch_result.modeling_clones.push_back({
          .source = source,
          .duplicate = duplicate,
          .source_modeling_object_id = source_modeling->object_id,
          .duplicate_modeling_object_id = duplicate_modeling->object_id,
      });
    }
    if (!duplicated.message.empty()) {
      duplicate_message = std::move(duplicated.message);
    }
    prepared.result.batches.push_back(std::move(batch_result));
  }

  if (contains_modeling_content) {
    const ModelingSessionMemento modeling_after =
        modeling.capture_history_memento();
    if (!modeling_after.valid()) {
      cancel_and_restore(command_invalid_result(
          "Structural duplicate could not capture its modeling history "
          "state."));
      return prepared;
    }
    const CommandResult recorded_modeling =
        transaction.record_executed_before_existing(
            std::make_unique<RecordedModelingMementoCommand>(
                modeling, label, prepared.modeling_before, modeling_after,
                true));
    if (!recorded_modeling.ok) {
      cancel_and_restore(recorded_modeling);
      return prepared;
    }
  }

  prepared.result.ok = true;
  prepared.result.changed = true;
  prepared.result.message = std::move(duplicate_message);
  return prepared;
}

} // namespace

ModelingStructuralDuplicateResult modeling_structural_duplicate_result(
    SceneStructuralDuplicateResult result) {
  ModelingStructuralDuplicateResult adapted;
  adapted.ok = result.ok;
  adapted.changed = result.changed;
  adapted.message = std::move(result.message);
  adapted.roots.reserve(result.clones.size());
  for (const SceneStructuralClone &clone : result.clones) {
    adapted.roots.push_back({
        .source = clone.source,
        .duplicate = clone.duplicate,
    });
  }
  adapted.modeling_objects.reserve(result.modeling_clones.size());
  for (const SceneStructuralModelingClone &clone : result.modeling_clones) {
    adapted.modeling_objects.push_back({
        .source = clone.source,
        .duplicate = clone.duplicate,
        .source_modeling_object_id = clone.source_modeling_object_id,
        .duplicate_modeling_object_id = clone.duplicate_modeling_object_id,
    });
  }
  return adapted;
}

SceneStructuralLifecycleService::SceneStructuralLifecycleService(
    SceneDocument &scene, EditorHistoryService &history,
    DocumentCommandService &document_commands,
    ModelingSessionService &modeling, EditorSelectionService *selection)
    : scene_(scene), history_(history), document_commands_(document_commands),
      modeling_(modeling), selection_(selection) {}

DocumentGroupSelectionResult
SceneStructuralLifecycleService::group_selection(
    std::string group_name, TransformComponent group_transform) {
  PreparedDocumentCommand prepared =
      document_commands_.prepare_group_selection_command(
          std::move(group_name), group_transform);
  DocumentGroupSelectionResult result;
  if (!prepared.command) {
    static_cast<DocumentCommandResult &>(result) = std::move(prepared.status);
    return result;
  }
  const auto accepted = document_commands_.accept_active_property_edit();
  if (!accepted.ok) {
    static_cast<DocumentCommandResult &>(result) = accepted;
    return result;
  }
  static_cast<DocumentCommandResult &>(result) = history_.execute(
      std::make_unique<ModelingSelectionMementoCommand>(
          modeling_, selection_, std::move(prepared.command)));
  result.changed = result.changed || accepted.changed;
  if (result.ok && result.changed) {
    result.group_entity = scene_.primary_selection();
  }
  return result;
}

DocumentCommandResult
SceneStructuralLifecycleService::ungroup_selection() {
  PreparedDocumentCommand prepared =
      document_commands_.prepare_ungroup_selection_command();
  if (!prepared.command) {
    return std::move(prepared.status);
  }
  const auto accepted = document_commands_.accept_active_property_edit();
  if (!accepted.ok) return accepted;
  auto result = history_.execute(
      std::make_unique<ModelingSelectionMementoCommand>(
          modeling_, selection_, std::move(prepared.command)));
  result.changed = result.changed || accepted.changed;
  return result;
}

std::vector<EntityId>
SceneStructuralLifecycleService::selected_roots() const {
  return document_command_selected_root_entities(scene_);
}

bool SceneStructuralLifecycleService::can_duplicate_selected_hierarchy() const {
  if (!document_commands_.object_mode_active()) {
    return false;
  }
  const std::vector<EntityId> roots = selected_roots();
  return !roots.empty() &&
         document_command_hierarchy_available_in_release(scene_, roots);
}

bool SceneStructuralLifecycleService::hierarchy_contains_modeling_content(
    std::span<const EntityId> roots) const {
  for (const EntityId root : roots) {
    if (root == kInvalidEntityId || !scene_.exists(root)) {
      continue;
    }
    for (const EntityId entity : scene_.descendants(root, true)) {
      if (scene_.modeling_object(entity) != nullptr) {
        return true;
      }
    }
  }
  return false;
}

SceneStructuralLifecycleService::NormalizedRoots
SceneStructuralLifecycleService::normalize_explicit_roots(
    std::span<const EntityId> roots) const {
  if (!document_commands_.object_mode_active()) {
    return {.status = command_invalid_result("Hierarchy editing requires Object Mode.")};
  }
  if (roots.empty()) {
    return {
        .status = command_invalid_result("No entity hierarchy was provided."),
    };
  }

  std::unordered_set<EntityId> requested;
  requested.reserve(roots.size());
  for (const EntityId root : roots) {
    if (root == kInvalidEntityId || !scene_.exists(root)) {
      return {.status = command_invalid_result("Entity does not exist")};
    }
    requested.insert(root);
  }

  std::vector<EntityId> normalized;
  normalized.reserve(requested.size());
  for (const EntityId entity : scene_.entity_tree_order()) {
    if (!requested.contains(entity)) {
      continue;
    }
    bool covered_by_requested_ancestor = false;
    EntityId parent = scene_.parent(entity);
    while (parent != kInvalidEntityId) {
      if (requested.contains(parent)) {
        covered_by_requested_ancestor = true;
        break;
      }
      parent = scene_.parent(parent);
    }
    if (!covered_by_requested_ancestor) {
      normalized.push_back(entity);
    }
  }

  if (normalized.empty()) {
    return {
        .status = command_invalid_result(
            "No existing entity hierarchy was provided."),
    };
  }
  return {
      .status = command_changed_result(),
      .roots = std::move(normalized),
  };
}

SceneStructuralDuplicateResult
SceneStructuralLifecycleService::duplicate_entity(EntityId entity) {
  const ModelingSelectionMode result_mode =
      modeling_.selection_mode() == ModelingSelectionMode::Group
          ? ModelingSelectionMode::Group
          : ModelingSelectionMode::Object;
  return duplicate_hierarchy(std::span<const EntityId>(&entity, 1U),
                             result_mode, "Duplicate Entity");
}

SceneStructuralDuplicateResult
SceneStructuralLifecycleService::duplicate_selected_hierarchy(
    ModelingSelectionMode result_selection_mode) {
  const std::vector<EntityId> roots = selected_roots();
  return duplicate_hierarchy(roots, result_selection_mode,
                             "Duplicate Selection");
}

SceneStructuralDuplicateResult
SceneStructuralLifecycleService::duplicate_hierarchy(
    std::span<const EntityId> roots,
    ModelingSelectionMode result_selection_mode, std::string label,
    bool independent_mesh_assets) {
  if (!document_command_hierarchy_available_in_release(scene_, roots)) {
    return duplicate_failure(
        command_invalid_result(release_feature_unavailable_message()));
  }

  QDR_PROFILE_FUNCTION();
  NormalizedRoots normalized = normalize_explicit_roots(roots);
  if (!normalized.status.ok) {
    return duplicate_failure(std::move(normalized.status));
  }
  if (result_selection_mode != ModelingSelectionMode::Group) {
    result_selection_mode = ModelingSelectionMode::Object;
  }
  if (label.empty()) {
    label = "Duplicate Selection";
  }
  const bool contains_modeling_content =
      hierarchy_contains_modeling_content(normalized.roots);
  const SceneStructuralDuplicateBatch batch{.roots = normalized.roots};
  if (history_.transaction_active()) {
    return duplicate_failure(command_invalid_result(
        "Cannot duplicate entities while another editor transaction is active."));
  }
  const auto accepted = document_commands_.accept_active_property_edit();
  if (!accepted.ok) return duplicate_failure(accepted);
  EditorTransaction transaction = history_.begin_transaction(label);
  if (!transaction.active()) {
    return duplicate_failure(command_invalid_result(
        "Cannot duplicate entities while another editor transaction is active."));
  }
  PreparedStructuralDuplicate prepared = prepare_structural_duplicate(
      scene_, modeling_, document_commands_, transaction,
      std::span<const SceneStructuralDuplicateBatch>(&batch, 1U),
      result_selection_mode, label, independent_mesh_assets,
      contains_modeling_content);
  if (!prepared.result.ok || !prepared.result.changed) {
    return duplicate_failure(CommandResult{
        .ok = prepared.result.ok,
        .changed = prepared.result.changed,
        .message = std::move(prepared.result.message),
    });
  }

  if (!scene_.acknowledge_scalar_compensation_checkpoint(
          prepared.scalar_checkpoint)) {
    const CommandResult preserved = transaction.commit();
    (void)scene_.discard_scalar_compensation_checkpoint(
        prepared.scalar_checkpoint);
    CommandResult failure = command_invalid_result(
        "Structural duplicate scalar compensation boundary could not be "
        "acknowledged.");
    failure.changed = true;
    if (!preserved.ok) {
      failure.message +=
          "; applied duplicate state could not be retained in history: ";
      failure.message += preserved.message;
    }
    return duplicate_failure(std::move(failure));
  }
  const CommandResult committed = transaction.commit();
  if (!scene_.begin_scalar_compensation_restore(
          prepared.scalar_checkpoint)) {
    (void)scene_.discard_scalar_compensation_checkpoint(
        prepared.scalar_checkpoint);
    CommandResult failure = command_invalid_result(
        "Scene changed while the structural duplicate was committed; "
        "rollback was rejected.");
    failure.changed = true;
    return duplicate_failure(std::move(failure));
  }
  if (!committed.ok || !committed.changed) {
    CommandResult document_rollback =
        remove_duplicate_roots(scene_, prepared.duplicate_roots);
    ModelingCommandResult modeling_rollback{
        .ok = true,
        .changed = false,
    };
    if (contains_modeling_content) {
      modeling_rollback =
          modeling_.restore_history_memento_exact_for_failure_compensation(
              prepared.modeling_before);
      if (!document_rollback.ok) {
        document_rollback =
            remove_duplicate_roots(scene_, prepared.duplicate_roots);
      }
    }
    document_command_apply_selection(scene_, prepared.selection_before);
    const bool selection_restored = document_command_same_selection(
        prepared.selection_before,
        document_command_capture_selection(scene_));
    CommandResult failure = committed;
    if (failure.ok) {
      failure = command_invalid_result(
          "Structural duplicate could not be committed to history.");
    }
    CommandResult result = duplicate_rollback_failure(
        std::move(failure), document_rollback, modeling_rollback);
    if (!document_rollback.ok || !modeling_rollback.ok ||
        !selection_restored) {
      (void)scene_.discard_scalar_compensation_checkpoint(
          prepared.scalar_checkpoint);
      result.ok = false;
      result.changed = true;
      if (!selection_restored) {
        if (!result.message.empty()) {
          result.message += "; ";
        }
        result.message +=
            "structural duplicate rollback did not restore the exact scene "
            "selection";
      }
    } else if (!scene_.restore_scalar_compensation_checkpoint(
                   prepared.scalar_checkpoint)) {
      result.ok = false;
      result.changed = true;
      if (!result.message.empty()) {
        result.message += "; ";
      }
      result.message += "structural duplicate scalar compensation failed";
    }
    return duplicate_failure(std::move(result));
  }
  if (!scene_.discard_scalar_compensation_checkpoint(
          prepared.scalar_checkpoint)) {
    CommandResult failure = command_invalid_result(
        "Structural duplicate scalar compensation scope could not be "
        "released.");
    failure.changed = true;
    return duplicate_failure(std::move(failure));
  }
  modeling_.record_repeatable_structural_duplicate();
  if (prepared.result.batches.size() != 1u) {
    CommandResult failure = command_invalid_result(
        "Structural duplicate returned an incomplete batch result.");
    failure.changed = true;
    return duplicate_failure(std::move(failure));
  }
  return std::move(prepared.result.batches.front());
}

SceneStructuralDuplicateResult
SceneStructuralLifecycleService::duplicate_hierarchy_in_transaction(
    EditorTransaction &transaction, std::span<const EntityId> roots,
    ModelingSelectionMode result_selection_mode, std::string label,
    bool independent_mesh_assets) {
  SceneStructuralDuplicateBatch batch{
      .roots = std::vector<EntityId>(roots.begin(), roots.end()),
  };
  SceneStructuralDuplicateBatchResult duplicated =
      duplicate_hierarchy_batches_in_transaction(
          transaction,
          std::span<const SceneStructuralDuplicateBatch>(&batch, 1U),
          result_selection_mode, std::move(label), independent_mesh_assets);
  if (!duplicated.ok || !duplicated.changed) {
    return duplicate_failure(CommandResult{
        .ok = duplicated.ok,
        .changed = duplicated.changed,
        .message = std::move(duplicated.message),
    });
  }
  if (duplicated.batches.size() != 1u) {
    CommandResult failure = command_invalid_result(
        "Structural duplicate returned an incomplete batch result.");
    failure.changed = true;
    return duplicate_failure(std::move(failure));
  }
  return std::move(duplicated.batches.front());
}

SceneStructuralDuplicateBatchResult
SceneStructuralLifecycleService::duplicate_hierarchy_batches_in_transaction(
    EditorTransaction &transaction,
    std::span<const SceneStructuralDuplicateBatch> batches,
    ModelingSelectionMode result_selection_mode, std::string label,
    bool independent_mesh_assets, SceneEntityNamePolicy name_policy) {
  for (const SceneStructuralDuplicateBatch &batch : batches) {
    if (!document_command_hierarchy_available_in_release(scene_, batch.roots)) {
      return duplicate_batch_failure(
          command_invalid_result(release_feature_unavailable_message()));
    }
  }

  QDR_PROFILE_FUNCTION();
  if (!transaction.active()) {
    return duplicate_batch_failure(
        command_invalid_result("Editor transaction is not active."));
  }
  if (!transaction.empty()) {
    return duplicate_batch_failure(command_invalid_result(
        "Structural duplicate requires an empty caller-owned transaction."));
  }
  if (batches.empty()) {
    return duplicate_batch_failure(
        command_invalid_result("No hierarchy duplicate batch was provided."));
  }

  std::vector<SceneStructuralDuplicateBatch> normalized_batches;
  normalized_batches.reserve(batches.size());
  bool contains_modeling_content = false;
  for (const SceneStructuralDuplicateBatch &batch : batches) {
    NormalizedRoots normalized = normalize_explicit_roots(batch.roots);
    if (!normalized.status.ok) {
      return duplicate_batch_failure(std::move(normalized.status));
    }
    contains_modeling_content =
        contains_modeling_content ||
        hierarchy_contains_modeling_content(normalized.roots);
    normalized_batches.push_back({.roots = std::move(normalized.roots)});
  }
  if (result_selection_mode != ModelingSelectionMode::Group) {
    result_selection_mode = ModelingSelectionMode::Object;
  }
  if (label.empty()) {
    label = "Duplicate Selection";
  }

  PreparedStructuralDuplicate prepared = prepare_structural_duplicate(
      scene_, modeling_, document_commands_, transaction, normalized_batches,
      result_selection_mode, label, independent_mesh_assets,
      contains_modeling_content, name_policy);
  if (prepared.result.ok && prepared.result.changed &&
      !scene_.discard_scalar_compensation_checkpoint(
          prepared.scalar_checkpoint)) {
    prepared.result = duplicate_batch_failure(command_invalid_result(
        "Structural duplicate scalar compensation scope could not be "
        "released."));
    prepared.result.changed = true;
  }
  return std::move(prepared.result);
}

CommandResult SceneStructuralLifecycleService::destroy_entity(EntityId entity) {
  return destroy_hierarchy(std::span<const EntityId>(&entity, 1U),
                           "Destroy Entity");
}

CommandResult
SceneStructuralLifecycleService::destroy_selected_hierarchy() {
  const std::vector<EntityId> roots = selected_roots();
  return destroy_hierarchy(roots, "Delete Selection");
}

CommandResult SceneStructuralLifecycleService::destroy_hierarchy(
    std::span<const EntityId> roots, std::string label) {
  QDR_PROFILE_FUNCTION();
  NormalizedRoots normalized = normalize_explicit_roots(roots);
  if (!normalized.status.ok) {
    return normalized.status;
  }
  const CommandResult property_accept =
      document_commands_.accept_active_property_edit();
  if (!property_accept.ok) {
    return property_accept;
  }
  if (label.empty()) {
    label = "Destroy Entities";
  }
  CommandResult destroyed = history_.execute(modeling_.create_destroy_entities_command(
      std::move(normalized.roots), std::move(label)));
  destroyed.changed = destroyed.changed || property_accept.changed;
  return destroyed;
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_mutation_runner.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "scene/scene_document.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace quader {

QuaderApiMutationRunner::QuaderApiMutationRunner(QuaderApiSession session)
    : session_(std::move(session)),
      observed_scene_session_id_(scene_session_id()),
      observed_dirty_(scene_session().dirty()) {}

bool QuaderApiMutationRunner::available() const {
  return QuaderApiSessionInternalAccess::active_scene_session(session_) !=
         nullptr;
}

SceneSessionId QuaderApiMutationRunner::scene_session_id() const {
  return QuaderApiSessionInternalAccess::scene_session_id(session_);
}

bool QuaderApiMutationRunner::belongs_to_active_scene(
    SceneSessionId scene_session_id) const {
  return QuaderApiSessionInternalAccess::belongs_to_active_scene(
      session_, scene_session_id);
}

bool QuaderApiMutationRunner::can_mutate_modeling_content() const {
  return available() &&
         !scene_session().native_tools().blocks_modeling_content_mutation();
}

QuaderApiVoidResult QuaderApiMutationRunner::modeling_content_blocked_status(
    std::string_view label) const {
  std::string message = "Apply or cancel the active tool before ";
  message.append(label.empty() ? "editing modeling content." : label);
  return quader_api_failure_status(scene_session(), std::move(message));
}

QuaderApiVoidResult QuaderApiMutationRunner::stale_scene_reference_status(
    std::string_view reference_kind) const {
  return quader_api_failure_status(
      scene_session(), std::string(reference_kind) +
                           " belongs to a closed, stale, or inactive scene.");
}

SceneSession &QuaderApiMutationRunner::scene_session() const {
  return QuaderApiSessionInternalAccess::scene_session(session_);
}

EditorHistoryService &QuaderApiMutationRunner::history() const {
  return scene_session().history();
}

EditorCommandService QuaderApiMutationRunner::editor_commands() const {
  SceneSession &session = scene_session();
  return EditorCommandService(session.history(), session.document_commands());
}

void QuaderApiMutationRunner::set_dirty_changed_callback(
    std::function<void(bool)> callback) {
  dirty_changed_callback_ = std::move(callback);
  synchronize_dirty_state();
}

void QuaderApiMutationRunner::synchronize_dirty_state() {
  observed_scene_session_id_ = scene_session_id();
  observed_dirty_ = scene_session().dirty();
}

QuaderRevisionStamp QuaderApiMutationRunner::current_revisions() const {
  return quader_api_current_revisions(scene_session());
}

EditorTransaction
QuaderApiMutationRunner::begin_transaction(std::string label) {
  return history().begin_transaction(std::move(label));
}

ModelingSessionMemento
QuaderApiMutationRunner::capture_modeling_memento() const {
  return scene_session().modeling().capture_history_memento();
}

QuaderApiVoidResult QuaderApiMutationRunner::restore_modeling_memento(
    const ModelingSessionMemento &memento, std::string_view label) {
  if (!can_mutate_modeling_content()) {
    return modeling_content_blocked_status(label);
  }
  return status_from(scene_session().modeling().restore_history_memento(memento),
                     QuaderMutationKind::Modeling, label);
}

QuaderApiResult<QuaderEntityRef>
QuaderApiMutationRunner::create_entity(const QuaderCreateEntityArgs &args) {
  if (!available()) {
    return quader_api_result_from_status<QuaderEntityRef>(
        quader_api_failure_status(scene_session(), "No scene is active."));
  }
  SceneSession &session = scene_session();
  DocumentCreateEntityResult command =
      session.document_commands().create_entity(
          args.name, to_scene_entity_kind(args.kind), args.parent,
          to_scene_transform(args.transform));
  const EntityId created_entity = command.entity;

  const QuaderApiVoidResult status =
      run_document_result(std::move(command), "Create Entity");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .scene_session_id = scene_session_id(),
        .id = created_entity,
        .uuid = session.scene().uuid(created_entity),
        .editor_object_id = session.scene().editor_object_id(created_entity),
    };
  }
  return result;
}

QuaderApiResult<QuaderEntityRef> QuaderApiMutationRunner::create_point_light(
    const QuaderCreatePointLightArgs &args) {
  if (!available()) {
    return quader_api_result_from_status<QuaderEntityRef>(
        quader_api_failure_status(scene_session(), "No scene is active."));
  }
  SceneSession &session = scene_session();
  DocumentCreateEntityResult command =
      session.document_commands().create_point_light(
          args.name, to_scene_light_component(args.light), args.parent,
          to_scene_transform(args.transform), args.select_created);
  const EntityId created_entity = command.entity;

  const QuaderApiVoidResult status =
      run_document_result(std::move(command), "Create Point Light");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .scene_session_id = scene_session_id(),
        .id = created_entity,
        .uuid = session.scene().uuid(created_entity),
        .editor_object_id = session.scene().editor_object_id(created_entity),
    };
  }
  return result;
}

QuaderApiResult<QuaderEntityRef>
QuaderApiMutationRunner::create_directional_light(
    const QuaderCreateDirectionalLightArgs &args) {
  if (!available()) {
    return quader_api_result_from_status<QuaderEntityRef>(
        quader_api_failure_status(scene_session(), "No scene is active."));
  }
  SceneSession &session = scene_session();
  DocumentCreateEntityResult command =
      session.document_commands().create_directional_light(
          args.name, to_scene_light_component(args.light), args.parent,
          to_scene_transform(args.transform), args.select_created);
  const EntityId created_entity = command.entity;

  const QuaderApiVoidResult status =
      run_document_result(std::move(command), "Create Directional Light");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .scene_session_id = scene_session_id(),
        .id = created_entity,
        .uuid = session.scene().uuid(created_entity),
        .editor_object_id = session.scene().editor_object_id(created_entity),
    };
  }
  return result;
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_document_command(CommandResult result,
                                              std::string_view label) {
  QDR_PROFILE_FUNCTION();
  return status_from(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::run_document_command(
    std::unique_ptr<IUndoableCommand> command, std::string_view label) {
  QDR_PROFILE_FUNCTION();
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  EditorCommandService commands = editor_commands();
  return status_from(commands.execute(std::move(command)),
                     QuaderMutationKind::Document, label);
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_document_result(DocumentCommandResult result,
                                             std::string_view label) {
  QDR_PROFILE_FUNCTION();
  return status_from(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_modeling_result(ModelingCommandResult result,
                                             std::string_view label) {
  QDR_PROFILE_FUNCTION();
  return status_from(std::move(result), QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::run_modeling_command(
    std::unique_ptr<ModelingMementoCommand> command, std::string_view label) {
  QDR_PROFILE_FUNCTION();
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  if (!can_mutate_modeling_content()) {
    return modeling_content_blocked_status(label);
  }
  EditorCommandService commands = editor_commands();
  return status_from(commands.execute_modeling(std::move(command)),
                     QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_runtime_result(CommandResult result,
                                            std::string_view label) {
  QDR_PROFILE_FUNCTION();
  return status_from(std::move(result), QuaderMutationKind::Runtime, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::run_modeling_undoable_command(
    std::unique_ptr<IUndoableCommand> command, std::string_view label,
    UndoRedoExecuteOptions options) {
  QDR_PROFILE_FUNCTION();
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  if (!can_mutate_modeling_content()) {
    return modeling_content_blocked_status(label);
  }
  EditorCommandService commands = editor_commands();
  return status_from(commands.history().execute(std::move(command), std::move(options)),
                     QuaderMutationKind::Modeling, label);
}

bool QuaderApiMutationRunner::can_run_tracked_modeling_tool(
    std::string_view tool_id) const {
  if (!available()) {
    return false;
  }
  SceneSession &session = scene_session();
  if (session.native_tools().blocks_modeling_content_mutation()) {
    return false;
  }
  MaterialReference active_material;
  if (ProjectSession *project =
          QuaderApiSessionInternalAccess::project_session(session_)) {
    if (const std::optional<ProjectAssetId> id =
            project->active_material_id()) {
      active_material = MaterialReference::project(*id);
    }
  }
  return modeling_commands_
      .availability(session.modeling(), tool_id, &session.selection(),
                    active_material, &session.structural_lifecycle())
      .enabled;
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_tracked_modeling_tool(std::string_view tool_id) {
  QDR_PROFILE_FUNCTION();
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  if (!can_mutate_modeling_content()) {
    return modeling_content_blocked_status("editing modeling content.");
  }
  SceneSession &session = scene_session();
  MaterialReference active_material;
  if (ProjectSession *project =
          QuaderApiSessionInternalAccess::project_session(session_)) {
    if (const std::optional<ProjectAssetId> id =
            project->active_material_id()) {
      active_material = MaterialReference::project(*id);
    }
  }
  return status_from(modeling_commands_.execute(history(), session.modeling(),
                                                 session.selection(), tool_id,
                                                 active_material,
                                                 &session.structural_lifecycle()),
                     QuaderMutationKind::Modeling, tool_id);
}

QuaderApiVoidResult QuaderApiMutationRunner::begin_tracked_modeling_preview(
    std::string_view action_id) {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  if (!can_mutate_modeling_content()) {
    return modeling_content_blocked_status("starting a modeling preview.");
  }
  SceneSession &session = scene_session();
  return status_from(
      modeling_commands_.begin_preview(history(), session.modeling(),
                                       session.selection(), action_id,
                                       &session.structural_lifecycle()),
      QuaderMutationKind::Modeling, action_id);
}

QuaderApiVoidResult
QuaderApiMutationRunner::commit_modeling_preview(std::string label) {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  SceneSession &session = scene_session();
  return status_from(modeling_commands_.commit_preview(
                         history(), session.modeling(), std::move(label)),
                     QuaderMutationKind::Modeling, "Commit Operation Preview");
}

QuaderApiVoidResult QuaderApiMutationRunner::cancel_modeling_preview() {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  return status_from(modeling_commands_.cancel_preview(
                         scene_session().modeling()),
                     QuaderMutationKind::Runtime, "Cancel Operation Preview");
}

QuaderApiVoidResult
QuaderApiMutationRunner::set_selection_mode(ModelingSelectionMode mode) {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  SceneSession &session = scene_session();
  bool changed = false;
  if (session.native_tools().has_pending_tool()) {
    const CommandResult canceled = session.native_tools().cancel_active();
    if (!canceled.ok) {
      return status_from(canceled, QuaderMutationKind::Runtime,
                         "Set Selection Mode");
    }
    changed = canceled.changed;
  }

  ModelingCommandResult result = session.selection().set_mode(mode);
  result.changed = result.changed || changed;
  return status_from(std::move(result), QuaderMutationKind::Modeling,
                     "Set Selection Mode");
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_asset_result(CommandResult result,
                                          std::string_view label) {
  return status_from(std::move(result), QuaderMutationKind::Asset, label);
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_asset_result(bool changed,
                                          std::string_view label) {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  QuaderApiVoidResult status =
      quader_api_void_status(scene_session(), true, changed, {},
                             QuaderMutationKind::Asset, label);
  observe_dirty_state();
  return status;
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_asset_failure(std::string message) {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  QuaderApiVoidResult status =
      quader_api_failure_status(scene_session(), std::move(message));
  observe_dirty_state();
  return status;
}

bool QuaderApiMutationRunner::can_undo() const {
  if (!available()) {
    return false;
  }
  SceneSession &session = scene_session();
  if (session.native_tools().has_pending_history_preview()) {
    return true;
  }
  if (session.modeling().can_cancel_active_preview_for_undo()) {
    return true;
  }
  return history().can_undo();
}

bool QuaderApiMutationRunner::can_redo() const {
  if (!available()) {
    return false;
  }
  SceneSession &session = scene_session();
  if (session.native_tools().has_pending_history_preview()) {
    return false;
  }
  if (session.modeling().can_cancel_active_preview_for_undo()) {
    return false;
  }
  return history().can_redo();
}

QuaderApiVoidResult QuaderApiMutationRunner::undo() {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  SceneSession &session = scene_session();
  if (session.native_tools().has_pending_history_preview()) {
    return status_from(session.native_tools().cancel_active(),
                       QuaderMutationKind::Runtime, "Undo");
  }
  if (session.modeling().can_cancel_active_preview_for_undo()) {
    const ModelingCommandResult cancel =
        cancel_active_tool_or_preview_for_undo();
    if (!cancel.ok || cancel.changed || !history().can_undo()) {
      return status_from(cancel, QuaderMutationKind::Runtime, "Undo");
    }
  }
  EditorCommandService commands = editor_commands();
  CommandResult result = commands.undo();
  return status_from(std::move(result), QuaderMutationKind::Document, "Undo");
}

QuaderApiVoidResult QuaderApiMutationRunner::redo() {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  if (!can_redo()) {
    return status_from(command_invalid_result("Redo is not available"),
                       QuaderMutationKind::Document, "Redo");
  }
  EditorCommandService commands = editor_commands();
  CommandResult result = commands.redo();
  return status_from(std::move(result), QuaderMutationKind::Document, "Redo");
}

QuaderApiVoidResult QuaderApiMutationRunner::repeat_last_action() {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  if (!can_mutate_modeling_content()) {
    return modeling_content_blocked_status("repeating a modeling action.");
  }
  SceneSession &session = scene_session();
  if (session.modeling().repeat_last_action_uses_structural_duplicate()) {
    const SceneSelectionState selection_before =
        capture_scene_selection(session.scene());
    SceneScalarCompensationCheckpoint scalar_checkpoint =
        session.scene().capture_scalar_compensation_checkpoint();
    if (!scalar_checkpoint.valid()) {
      return status_from(
          command_invalid_result(
              "Scene scalar compensation scope is unavailable."),
          QuaderMutationKind::Modeling, "Repeat Last Action");
    }
    EditorTransaction transaction =
        session.history().begin_transaction("Repeat Last Action");
    if (!transaction.active()) {
      return status_from(
          command_invalid_result(
              "Cannot repeat while another editor transaction is active."),
          QuaderMutationKind::Modeling, "Repeat Last Action");
    }
    const ModelingStructuralDuplicate structural_duplicate =
        [&session](EditorTransaction &active_transaction,
                   ModelingSelectionMode selection_mode,
                   std::string_view label) {
          return modeling_structural_duplicate_result(
              session.structural_lifecycle()
                  .duplicate_hierarchy_in_transaction(
                      active_transaction,
                      session.structural_lifecycle().selected_roots(),
                      selection_mode, std::string(label)));
        };
    ModelingCommandResult repeated =
        session.modeling().repeat_last_structural_transform(
            transaction, session.history(), structural_duplicate);
    if (!repeated.ok || !repeated.changed) {
      if (!session.scene().acknowledge_scalar_compensation_checkpoint(
              scalar_checkpoint) ||
          !session.scene().begin_scalar_compensation_restore(
              scalar_checkpoint)) {
        (void)session.scene().discard_scalar_compensation_checkpoint(
            scalar_checkpoint);
        const CommandResult preserved =
            transaction.active() ? transaction.commit()
                                 : command_unchanged_result();
        repeated.ok = false;
        repeated.changed = true;
        if (!repeated.message.empty()) {
          repeated.message += "; ";
        }
        repeated.message +=
            "Repeat rollback was rejected because the Scene changed outside "
            "the compensation scope.";
        if (!preserved.ok) {
          repeated.message +=
              "; applied repeat state could not be retained in history: ";
          repeated.message += preserved.message;
        }
        return status_from(std::move(repeated), QuaderMutationKind::Modeling,
                           "Repeat Last Action");
      }
      // The structural lifecycle owns rollback of the commands it appended
      // when duplication itself fails. In that case the caller-owned
      // transaction is already inactive and must not be canceled a second
      // time, because an inactive cancel is itself an error.
      const CommandResult rollback =
          transaction.active() ? transaction.cancel()
                               : command_unchanged_result();
      const bool selection_restored = same_scene_selection(
          selection_before, capture_scene_selection(session.scene()));
      if (!rollback.ok || !selection_restored) {
        (void)session.scene().discard_scalar_compensation_checkpoint(
            scalar_checkpoint);
        repeated.ok = false;
        repeated.changed = true;
        const std::string rollback_message =
            !rollback.ok
                ? "Repeat failed and rollback failed: " + rollback.message
                : "Repeat rollback did not restore the exact scene selection.";
        repeated.message = repeated.message.empty()
                               ? rollback_message
                               : repeated.message + "; " + rollback_message;
      } else if (!session.scene().restore_scalar_compensation_checkpoint(
                     scalar_checkpoint)) {
        repeated.ok = false;
        repeated.changed = true;
        if (!repeated.message.empty()) {
          repeated.message += "; ";
        }
        repeated.message += "Repeat scalar compensation failed.";
      } else {
        repeated.changed = false;
      }
      return status_from(std::move(repeated), QuaderMutationKind::Modeling,
                         "Repeat Last Action");
    }
    if (!session.scene().acknowledge_scalar_compensation_checkpoint(
            scalar_checkpoint)) {
      const CommandResult committed = transaction.commit();
      (void)session.scene().discard_scalar_compensation_checkpoint(
          scalar_checkpoint);
      CommandResult failure = command_invalid_result(
          "Repeat scalar compensation boundary could not be acknowledged.");
      failure.changed = true;
      if (!committed.ok) {
        failure.message += "; applied repeat state could not be retained in "
                           "history: ";
        failure.message += committed.message;
      }
      return status_from(std::move(failure), QuaderMutationKind::Modeling,
                         "Repeat Last Action");
    }
    const CommandResult committed = transaction.commit();
    const bool scene_unchanged_during_commit =
        session.scene().begin_scalar_compensation_restore(scalar_checkpoint);
    (void)session.scene().discard_scalar_compensation_checkpoint(
        scalar_checkpoint);
    if (!scene_unchanged_during_commit) {
      CommandResult failure = command_invalid_result(
          "Scene changed while Repeat Last Action was committed; rollback "
          "was rejected.");
      failure.changed = true;
      return status_from(std::move(failure), QuaderMutationKind::Modeling,
                         "Repeat Last Action");
    }
    if (!committed.ok || !committed.changed) {
      return status_from(
          committed.ok
              ? command_invalid_result(
                    "Repeat Last Action could not be committed to history.")
              : committed,
          QuaderMutationKind::Modeling, "Repeat Last Action");
    }
    return status_from(std::move(repeated), QuaderMutationKind::Modeling,
                       "Repeat Last Action");
  }
  EditorCommandService commands = editor_commands();
  return status_from(commands.execute_modeling(
                         std::make_unique<RepeatLastActionHistoryCommand>(
                             session.modeling())),
                     QuaderMutationKind::Modeling, "Repeat Last Action");
}

QuaderApiVoidResult
QuaderApiMutationRunner::status_from(CommandResult result,
                                     QuaderMutationKind kind,
                                     std::string_view fallback_message) {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  QuaderApiVoidResult status =
      quader_api_void_status(scene_session(), result.ok, result.changed,
                             std::move(result.message), kind,
                             fallback_message);
  observe_dirty_state();
  return status;
}

QuaderApiVoidResult
QuaderApiMutationRunner::status_from(ModelingCommandResult result,
                                     QuaderMutationKind kind,
                                     std::string_view fallback_message) {
  if (!available()) {
    return quader_api_failure_status(scene_session(), "No scene is active.");
  }
  QuaderApiVoidResult status =
      quader_api_void_status(scene_session(), result.ok, result.changed,
                             std::move(result.message), kind,
                             fallback_message);
  observe_dirty_state();
  return status;
}

void QuaderApiMutationRunner::observe_dirty_state() {
  const SceneSessionId current_id = scene_session_id();
  const bool dirty = scene_session().dirty();
  if (current_id == observed_scene_session_id_ && dirty == observed_dirty_) {
    return;
  }
  observed_scene_session_id_ = current_id;
  observed_dirty_ = dirty;
  if (dirty_changed_callback_) {
    dirty_changed_callback_(dirty);
  }
}

ModelingCommandResult
QuaderApiMutationRunner::cancel_active_tool_or_preview_for_undo() {
  SceneSession &session = scene_session();
  if (session.modeling().operation_preview_active()) {
    return modeling_commands_.cancel_preview(session.modeling());
  }

  ModelingCommandResult cancel = session.modeling().cancel_transform_drag();
  if (!cancel.ok || cancel.changed) {
    return cancel;
  }

  return {.ok = true, .changed = false};
}

} // namespace quader

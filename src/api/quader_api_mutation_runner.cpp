////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_mutation_runner.hpp"

#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "diagnostics/profile.hpp"
#include "editor/session/project_session.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include <memory>

#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

/**
 * Captures scene selection before history replay so modeling selection can be reprojected when needed.
 */
struct SceneSelectionSnapshot {
    std::vector<EntityId> selected;
    EntityId active = kInvalidEntityId;
};

SceneSelectionSnapshot capture_scene_selection(const ProjectSession &session) {
  return {
      .selected = session.scene().selected_entities(),
      .active = session.scene().primary_selection(),
  };
}

bool scene_selection_changed(const SceneSelectionSnapshot &before,
                             const ProjectSession &session) {
  return before.selected != session.scene().selected_entities() ||
         before.active != session.scene().primary_selection();
}

bool scene_selection_contains_non_modeling_entity(
    const ProjectSession &session) {
  for (const EntityId entity : session.scene().selected_entities()) {
    if (session.scene().modeling_object(entity) == nullptr) {
      return true;
    }
  }
  return false;
}

void project_object_selection_after_history_selection_change(
    ProjectSession &session, const SceneSelectionSnapshot &before,
    const CommandResult &result) {
  if (!result.ok || !result.changed ||
      !scene_selection_changed(before, session)) {
    return;
  }

  if (scene_selection_contains_non_modeling_entity(session)) {
    session.modeling().project_object_selection_from_scene(true);
    return;
  }

  const ModelingSelectionSummary summary =
      session.modeling().selection_summary();
  if (!summary.has_selection ||
      selection_mode_uses_scene_document(summary.mode)) {
    session.modeling().project_object_selection_from_scene();
  }
}

} // namespace

QuaderApiMutationRunner::QuaderApiMutationRunner(
    ProjectSession &session, EditorHistoryService &history,
    EditorCommandService &editor_commands,
    ModelingCommandExecutionService &modeling_commands)
    : session_(session), history_(history), editor_commands_(editor_commands),
      modeling_commands_(modeling_commands) {}

QuaderRevisionStamp QuaderApiMutationRunner::current_revisions() const {
  return quader_api_current_revisions(session_);
}

EditorTransaction
QuaderApiMutationRunner::begin_transaction(std::string label) {
  return history_.begin_transaction(std::move(label));
}

ModelingSessionMemento
QuaderApiMutationRunner::capture_modeling_memento() const {
  return session_.modeling().capture_history_memento();
}

QuaderApiVoidResult QuaderApiMutationRunner::restore_modeling_memento(
    const ModelingSessionMemento &memento, std::string_view label) {
  return status_from(session_.modeling().restore_history_memento(memento),
                     QuaderMutationKind::Modeling, label);
}

QuaderApiResult<QuaderEntityRef>
QuaderApiMutationRunner::create_entity(const QuaderCreateEntityArgs &args) {
  DocumentCreateEntityResult command =
      session_.document_commands().create_entity(
          args.name, to_scene_entity_kind(args.kind), args.parent,
          to_scene_transform(args.transform));
  const EntityId created_entity = command.entity;

  const QuaderApiVoidResult status =
      run_document_result(std::move(command), "Create Entity");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .id = created_entity,
        .uuid = session_.scene().uuid(created_entity),
        .editor_object_id = session_.scene().editor_object_id(created_entity),
    };
  }
  return result;
}

QuaderApiResult<QuaderEntityRef> QuaderApiMutationRunner::create_point_light(
    const QuaderCreatePointLightArgs &args) {
  DocumentCreateEntityResult command =
      session_.document_commands().create_point_light(
          args.name, to_scene_light_component(args.light), args.parent,
          to_scene_transform(args.transform), args.select_created);
  const EntityId created_entity = command.entity;
  if (command.ok && command.changed && args.select_created) {
    session_.modeling().project_object_selection_from_scene(true);
  }

  const QuaderApiVoidResult status =
      run_document_result(std::move(command), "Create Point Light");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .id = created_entity,
        .uuid = session_.scene().uuid(created_entity),
        .editor_object_id = session_.scene().editor_object_id(created_entity),
    };
  }
  return result;
}

QuaderApiResult<QuaderEntityRef>
QuaderApiMutationRunner::create_directional_light(
    const QuaderCreateDirectionalLightArgs &args) {
  DocumentCreateEntityResult command =
      session_.document_commands().create_directional_light(
          args.name, to_scene_light_component(args.light), args.parent,
          to_scene_transform(args.transform), args.select_created);
  const EntityId created_entity = command.entity;
  if (command.ok && command.changed && args.select_created) {
    session_.modeling().project_object_selection_from_scene(true);
  }

  const QuaderApiVoidResult status =
      run_document_result(std::move(command), "Create Directional Light");
  QuaderApiResult<QuaderEntityRef> result =
      quader_api_result_from_status<QuaderEntityRef>(status);
  if (result.ok && result.changed) {
    result.value = QuaderEntityRef{
        .id = created_entity,
        .uuid = session_.scene().uuid(created_entity),
        .editor_object_id = session_.scene().editor_object_id(created_entity),
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
  return status_from(editor_commands_.execute(std::move(command)),
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
  return status_from(editor_commands_.execute_modeling(std::move(command)),
                     QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::run_modeling_undoable_command(
    std::unique_ptr<IUndoableCommand> command, std::string_view label) {
  QDR_PROFILE_FUNCTION();
  return status_from(editor_commands_.execute(std::move(command)),
                     QuaderMutationKind::Modeling, label);
}

bool QuaderApiMutationRunner::can_run_tracked_modeling_tool(
    std::string_view tool_id) const {
  return modeling_commands_
      .availability(session_.modeling(), tool_id, &session_.selection())
      .enabled;
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_tracked_modeling_tool(std::string_view tool_id) {
  QDR_PROFILE_FUNCTION();
  return status_from(modeling_commands_.execute(history_, session_.modeling(),
                                                session_.selection(), tool_id),
                     QuaderMutationKind::Modeling, tool_id);
}

QuaderApiVoidResult QuaderApiMutationRunner::begin_tracked_modeling_preview(
    std::string_view action_id) {
  return status_from(
      modeling_commands_.begin_preview(history_, session_.modeling(),
                                       session_.selection(), action_id),
      QuaderMutationKind::Modeling, action_id);
}

QuaderApiVoidResult
QuaderApiMutationRunner::commit_modeling_preview(std::string label) {
  return status_from(modeling_commands_.commit_preview(
                         history_, session_.modeling(), std::move(label)),
                     QuaderMutationKind::Modeling, "Commit Operation Preview");
}

QuaderApiVoidResult QuaderApiMutationRunner::cancel_modeling_preview() {
  return status_from(modeling_commands_.cancel_preview(session_.modeling()),
                     QuaderMutationKind::Runtime, "Cancel Operation Preview");
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_asset_result(CommandResult result,
                                          std::string_view label) {
  return status_from(std::move(result), QuaderMutationKind::Asset, label);
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_asset_result(bool changed,
                                          std::string_view label) {
  return quader_api_void_status(session_, true, changed, {},
                                QuaderMutationKind::Asset, label);
}

QuaderApiVoidResult
QuaderApiMutationRunner::run_asset_failure(std::string message) {
  return quader_api_failure_status(session_, std::move(message));
}

bool QuaderApiMutationRunner::can_undo() const {
  if (session_.modeling().can_cancel_active_preview_for_undo()) {
    return true;
  }
  return history_.can_undo();
}

bool QuaderApiMutationRunner::can_redo() const {
  if (session_.modeling().can_cancel_active_preview_for_undo()) {
    return false;
  }
  return history_.can_redo();
}

QuaderApiVoidResult QuaderApiMutationRunner::undo() {
  if (session_.modeling().can_cancel_active_preview_for_undo()) {
    const ModelingCommandResult cancel =
        cancel_active_tool_or_preview_for_undo();
    if (!cancel.ok || cancel.changed || !history_.can_undo()) {
      return status_from(cancel, QuaderMutationKind::Runtime, "Undo");
    }
  }
  const SceneSelectionSnapshot before = capture_scene_selection(session_);
  CommandResult result = editor_commands_.undo();
  project_object_selection_after_history_selection_change(session_, before,
                                                          result);
  return status_from(std::move(result), QuaderMutationKind::Document, "Undo");
}

QuaderApiVoidResult QuaderApiMutationRunner::redo() {
  if (!can_redo()) {
    return status_from(command_invalid_result("Redo is not available"),
                       QuaderMutationKind::Document, "Redo");
  }
  const SceneSelectionSnapshot before = capture_scene_selection(session_);
  CommandResult result = editor_commands_.redo();
  project_object_selection_after_history_selection_change(session_, before,
                                                          result);
  return status_from(std::move(result), QuaderMutationKind::Document, "Redo");
}

QuaderApiVoidResult QuaderApiMutationRunner::repeat_last_action() {
  return status_from(editor_commands_.execute_modeling(
                         std::make_unique<RepeatLastActionHistoryCommand>(
                             session_.modeling())),
                     QuaderMutationKind::Modeling, "Repeat Last Action");
}

QuaderApiVoidResult
QuaderApiMutationRunner::status_from(CommandResult result,
                                     QuaderMutationKind kind,
                                     std::string_view fallback_message) const {
  return quader_api_void_status(session_, result.ok, result.changed,
                                std::move(result.message), kind,
                                fallback_message);
}

QuaderApiVoidResult
QuaderApiMutationRunner::status_from(ModelingCommandResult result,
                                     QuaderMutationKind kind,
                                     std::string_view fallback_message) const {
  return quader_api_void_status(session_, result.ok, result.changed,
                                std::move(result.message), kind,
                                fallback_message);
}

ModelingCommandResult
QuaderApiMutationRunner::cancel_active_tool_or_preview_for_undo() {
  if (session_.modeling().tool_settings_target().operation_preview_active) {
    return modeling_commands_.cancel_preview(session_.modeling());
  }

  ModelingCommandResult cancel = session_.modeling().cancel_transform_drag();
  if (!cancel.ok || cancel.changed) {
    return cancel;
  }

  return {.ok = true, .changed = false};
}

} // namespace quader

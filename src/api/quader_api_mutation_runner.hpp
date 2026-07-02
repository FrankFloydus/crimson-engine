////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "core/command_result.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/commands/editor_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/commands/modeling_command_execution_service.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace quader {

class ProjectSession;
class ModelingSessionMemento;

/**
 * Owns Quader API Mutation Runner behavior within the public Quader API surface.
 */
class QuaderApiMutationRunner final {
public:
  QuaderApiMutationRunner(ProjectSession &session,
                          EditorHistoryService &history,
                          EditorCommandService &editor_commands,
                          ModelingCommandExecutionService &modeling_commands);

  [[nodiscard]] QuaderRevisionStamp current_revisions() const;
  [[nodiscard]] EditorTransaction begin_transaction(std::string label);
  [[nodiscard]] ModelingSessionMemento capture_modeling_memento() const;
  QuaderApiVoidResult
  restore_modeling_memento(const ModelingSessionMemento &memento,
                           std::string_view label);

  QuaderApiResult<QuaderEntityRef>
  create_entity(const QuaderCreateEntityArgs &args);
  QuaderApiResult<QuaderEntityRef>
  create_point_light(const QuaderCreatePointLightArgs &args);
  QuaderApiResult<QuaderEntityRef>
  create_directional_light(const QuaderCreateDirectionalLightArgs &args);
  QuaderApiVoidResult run_document_command(CommandResult result,
                                           std::string_view label);
  QuaderApiVoidResult
  run_document_command(std::unique_ptr<IUndoableCommand> command,
                       std::string_view label);
  QuaderApiVoidResult run_document_result(DocumentCommandResult result,
                                          std::string_view label);
  QuaderApiVoidResult run_modeling_result(ModelingCommandResult result,
                                          std::string_view label);
  QuaderApiVoidResult
  run_modeling_command(std::unique_ptr<ModelingMementoCommand> command,
                       std::string_view label);
  QuaderApiVoidResult
  run_modeling_undoable_command(std::unique_ptr<IUndoableCommand> command,
                                std::string_view label);
  [[nodiscard]] bool
  can_run_tracked_modeling_tool(std::string_view tool_id) const;
  QuaderApiVoidResult run_tracked_modeling_tool(std::string_view tool_id);
  QuaderApiVoidResult
  begin_tracked_modeling_preview(std::string_view action_id);
  QuaderApiVoidResult commit_modeling_preview(std::string label);
  QuaderApiVoidResult cancel_modeling_preview();
  QuaderApiVoidResult run_asset_result(bool changed, std::string_view label);
  QuaderApiVoidResult run_asset_failure(std::string message);
  [[nodiscard]] bool can_undo() const;
  [[nodiscard]] bool can_redo() const;
  QuaderApiVoidResult undo();
  QuaderApiVoidResult redo();
  QuaderApiVoidResult repeat_last_action();

private:
  [[nodiscard]] QuaderApiVoidResult
  status_from(CommandResult result, QuaderMutationKind kind,
              std::string_view fallback_message = {}) const;
  [[nodiscard]] QuaderApiVoidResult
  status_from(ModelingCommandResult result, QuaderMutationKind kind,
              std::string_view fallback_message = {}) const;
  [[nodiscard]] ModelingCommandResult cancel_active_tool_or_preview_for_undo();

  ProjectSession &session_;
  EditorHistoryService &history_;
  EditorCommandService &editor_commands_;
  ModelingCommandExecutionService &modeling_commands_;
};

} // namespace quader

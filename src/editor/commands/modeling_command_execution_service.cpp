////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling_command_execution_service.hpp"

#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/history/modeling_preview_command_service.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

/**
 * Applies Dispatched Modeling Tool History Command behavior as a command in the editor coordination layer.
 */
class DispatchedModelingToolHistoryCommand final : public ModelingMementoCommand {
public:
  DispatchedModelingToolHistoryCommand(
      ModelingSessionService &modeling, EditorSelectionService &selection,
      const ModelingCommandDispatcher &dispatcher, std::string tool_id,
      std::string label)
      : ModelingMementoCommand(modeling, std::move(label)),
        selection_(selection), dispatcher_(dispatcher),
        tool_id_(std::move(tool_id)) {}

private:
  ModelingCommandResult execute_initial() override {
    return dispatcher_.execute(tool_id_, modeling(), &selection_);
  }

    EditorSelectionService& selection_;
    const ModelingCommandDispatcher& dispatcher_;
    std::string tool_id_;
};

/**
 * Applies Modeling Action History Command behavior as a command in the editor coordination layer.
 */
class ModelingActionHistoryCommand final : public ModelingMementoCommand {
public:
  ModelingActionHistoryCommand(ModelingSessionService &modeling,
                               std::string action_id, std::string label)
      : ModelingMementoCommand(modeling, std::move(label)),
        action_id_(std::move(action_id)) {}

private:
  ModelingCommandResult execute_initial() override {
    return modeling().execute_context_action(action_id_);
  }

  std::string action_id_;
};

ModelingCommandResult from_command_result(CommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
}

std::string command_label(const CommandTool *tool, std::string_view tool_id) {
  if (tool == nullptr || tool->label().empty()) {
    return std::string(tool_id);
  }
  return std::string(tool->label());
}

} // namespace

ModelingCommandResult ModelingCommandExecutionService::execute(
    EditorHistoryService &history, ModelingSessionService &modeling,
    EditorSelectionService &selection, std::string_view tool_id) {
  const CommandTool *tool = dispatcher_.find(tool_id);
  if (tool == nullptr) {
    return {.ok = false, .message = "Command tool is not registered."};
  }

  if (tool->id() == "delete_selection" &&
      scene_aware_delete_handles_selection(
          modeling.selection_summary(),
          modeling.has_selected_scene_aware_delete_root())) {
    return from_command_result(
        history.execute(modeling.create_delete_selection_command()));
  }

  const ModelingCommandAvailability available =
      dispatcher_.availability(tool_id, modeling, &selection);
  if (!available.enabled) {
    return {
        .ok = false,
        .message = available.disabled_reason.empty()
                       ? "Command is disabled."
                       : available.disabled_reason,
    };
  }

  if (tool->history_policy().mode !=
      ModelingCommandHistoryMode::ModelingUndoRedo) {
    return dispatcher_.execute(*tool, modeling, &selection);
  }

  return from_command_result(
      history.execute(std::make_unique<DispatchedModelingToolHistoryCommand>(
          modeling, selection, dispatcher_, std::string(tool_id),
          command_label(tool, tool_id))));
}

ModelingCommandResult ModelingCommandExecutionService::execute_untracked(
    ModelingSessionService &modeling, std::string_view tool_id,
    EditorSelectionService *selection) {
  return dispatcher_.execute(tool_id, modeling, selection);
}

ModelingCommandResult ModelingCommandExecutionService::begin_preview(
    EditorHistoryService &history, ModelingSessionService &modeling,
    EditorSelectionService &selection, std::string_view action_id) {
  if (action_id == std::string_view("delete_selection")) {
    return execute(history, modeling, selection, action_id);
  }

  const std::vector<ModelingActionItem> actions =
      modeling.context_actions(ModelingActionQuery{
          .surface = ModelingActionSurface::ViewportContextMenu,
          .include_disabled = true,
          .include_hidden = false,
      });
  const auto found = std::find_if(actions.begin(), actions.end(),
                                  [&](const ModelingActionItem &action) {
                                    return action.action_id == action_id;
                                  });
  if (found == actions.end()) {
    return {.ok = false,
            .message = "Action is not available in the current mode."};
  }
  if (found->preview_kind != ModelingActionPreviewKind::None) {
    return modeling.execute_context_action(action_id);
  }
  return from_command_result(
      history.execute(std::make_unique<ModelingActionHistoryCommand>(
          modeling, std::string(action_id),
          found->label.empty() ? std::string(action_id) : found->label)));
}

ModelingCommandResult ModelingCommandExecutionService::begin_preview_untracked(
    ModelingSessionService &modeling, std::string_view action_id) {
  return modeling.execute_context_action(action_id);
}

ModelingCommandResult ModelingCommandExecutionService::commit_preview(
    EditorHistoryService &history, ModelingSessionService &modeling,
    std::string label) {
  if (label.empty()) {
    label = "Commit Operation Preview";
  }
  return ModelingPreviewCommandService::commit(history, modeling,
                                               std::move(label));
}

ModelingCommandResult ModelingCommandExecutionService::cancel_preview(
    ModelingSessionService &modeling) {
  return ModelingPreviewCommandService::cancel(modeling);
}

ModelingCommandAvailability ModelingCommandExecutionService::availability(
    ModelingSessionService &modeling, std::string_view tool_id,
    EditorSelectionService *selection) const {
  if (tool_id == std::string_view("delete_selection") &&
      scene_aware_delete_handles_selection(
          modeling.selection_summary(),
          modeling.has_selected_scene_aware_delete_root())) {
    return ModelingCommandAvailability::available();
  }
  return dispatcher_.availability(tool_id, modeling, selection);
}

ModelingCommandHistoryPolicy ModelingCommandExecutionService::history_policy(
    std::string_view tool_id) const {
  return dispatcher_.history_policy(tool_id);
}

} // namespace quader

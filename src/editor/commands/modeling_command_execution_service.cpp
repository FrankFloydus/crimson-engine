////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling_command_execution_service.hpp"

#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/history/modeling_preview_command_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

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

/** Records one accepted viewport modeling edit with its group selection. */
class ViewportModelingToolHistoryCommand final : public ModelingMementoCommand {
public:
  ViewportModelingToolHistoryCommand(ModelingSessionService &modeling,
                                    ModelingViewportTool tool, std::string label,
                                    MaterialReference creation_material)
      : ModelingMementoCommand(modeling, std::move(label)), tool_(tool),
        creation_material_(creation_material) {}

private:
  ModelingSessionMemento capture_before_memento() override {
    return modeling().capture_history_memento(true);
  }

  ModelingSessionMemento capture_after_memento() override {
    return modeling().capture_history_memento(true);
  }

  ModelingCommandResult execute_initial() override {
    const std::uint64_t content_before = modeling().content_revision();
    ModelingCommandResult result;
    switch (tool_) {
    case ModelingViewportTool::InsertEdgeLoop:
      result = modeling().commit_insert_edge_loop_drag();
      break;
    case ModelingViewportTool::Cut:
      result = modeling().finalize_cut_tool();
      break;
    case ModelingViewportTool::Knife:
      result = modeling().commit_knife_tool();
      break;
    case ModelingViewportTool::Poly:
      result = modeling().commit_poly_tool(creation_material_);
      break;
    case ModelingViewportTool::Mirror:
      result = modeling().finalize_mirror_tool();
      break;
    default:
      return {.ok = false,
              .message = "Viewport tool has no modeling commit operation."};
    }
    // Returning to Select changes runtime state but is not an authored edit.
    result.changed = modeling().content_revision() != content_before;
    return result;
  }

  ModelingViewportTool tool_;
  MaterialReference creation_material_;
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
    EditorSelectionService &selection, std::string_view tool_id,
    SceneStructuralLifecycleService *structural_lifecycle) {
  const CommandTool *tool = dispatcher_.find(tool_id);
  if (tool == nullptr) {
    return {.ok = false, .message = "Command tool is not registered."};
  }

  // Provider-owned controls participate in the canonical editor selection,
  // so Delete must resolve them before considering the selected owner object.
  // The provider remains responsible for its authored history transaction.
  if (tool->id() == "delete_selection") {
    if (auto sub_selection = selection.try_delete_sub_selection()) {
      return *std::move(sub_selection);
    }
  }

  if (tool->id() == "delete_selection" &&
      scene_aware_delete_handles_selection(
          modeling.selection_summary(),
          modeling.has_selected_scene_aware_delete_root())) {
    return from_command_result(
        structural_lifecycle != nullptr
            ? structural_lifecycle->destroy_selected_hierarchy()
            : history.execute(modeling.create_delete_selection_command()));
  }

  if (tool->id() == "duplicate_meshes") {
    const ModelingSelectionMode mode = modeling.selection_summary().mode;
    if (mode != ModelingSelectionMode::Object &&
        mode != ModelingSelectionMode::Group) {
      return {
          .ok = false,
          .message = "Duplicate Selection is available in Object or Group "
                     "mode.",
      };
    }
    if (structural_lifecycle == nullptr) {
      return {
          .ok = false,
          .message = "Scene structural lifecycle is unavailable.",
      };
    }
    if (!structural_lifecycle->can_duplicate_selected_hierarchy()) {
      return {
          .ok = false,
          .message = "Duplicate Selection needs a selected hierarchy.",
      };
    }
    return from_command_result(structural_lifecycle->duplicate_hierarchy(
        structural_lifecycle->selected_roots(), mode,
        command_label(tool, tool_id), true));
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
  if (tool_id == std::string_view("delete_selection") &&
      selection != nullptr) {
    if (auto sub_selection = selection->try_delete_sub_selection()) {
      return *std::move(sub_selection);
    }
  }
  return dispatcher_.execute(tool_id, modeling, selection);
}

ModelingCommandResult ModelingCommandExecutionService::begin_preview(
    EditorHistoryService &history, ModelingSessionService &modeling,
    EditorSelectionService &selection, std::string_view action_id,
    SceneStructuralLifecycleService *structural_lifecycle) {
  if (action_id == std::string_view("delete_selection") ||
      action_id == std::string_view("duplicate_meshes")) {
    return execute(history, modeling, selection, action_id,
                   structural_lifecycle);
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

ModelingCommandResult ModelingCommandExecutionService::commit_viewport_tool(
    EditorHistoryService &history, ModelingSessionService &modeling,
    ModelingViewportTool tool, std::string label,
    MaterialReference creation_material) {
  switch (tool) {
  case ModelingViewportTool::InsertEdgeLoop:
  case ModelingViewportTool::Cut:
  case ModelingViewportTool::Knife:
  case ModelingViewportTool::Poly:
  case ModelingViewportTool::Mirror:
    break;
  default:
    return {.ok = false,
            .message = "Viewport tool has no modeling commit operation."};
  }
  if (modeling.tool_status().active_tool != tool) {
    return {.ok = false,
            .message = "The requested viewport modeling tool is not active."};
  }
  return from_command_result(
      history.execute(std::make_unique<ViewportModelingToolHistoryCommand>(
          modeling, tool, std::move(label), creation_material)));
}

ModelingCommandAvailability ModelingCommandExecutionService::availability(
    ModelingSessionService &modeling, std::string_view tool_id,
    EditorSelectionService *selection,
    SceneStructuralLifecycleService *structural_lifecycle) const {
  if (tool_id == std::string_view("delete_selection") &&
      selection != nullptr && selection->has_sub_selection()) {
    return ModelingCommandAvailability::available();
  }
  if (tool_id == std::string_view("duplicate_meshes") &&
      structural_lifecycle != nullptr) {
    const ModelingSelectionMode mode = modeling.selection_summary().mode;
    if (mode != ModelingSelectionMode::Object &&
        mode != ModelingSelectionMode::Group) {
      return ModelingCommandAvailability::unavailable(
          "Duplicate Selection is available in Object or Group mode.");
    }
    return structural_lifecycle->can_duplicate_selected_hierarchy()
               ? ModelingCommandAvailability::available()
               : ModelingCommandAvailability::unavailable(
                     "Duplicate Selection needs a selected hierarchy.");
  }
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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling_command_execution_service.hpp"
#include "core/release_features.hpp"
#include "editor/metadata/release_feature_requirements.hpp"

#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/history/modeling_preview_command_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"

#include <editor/metadata/host_contract.hpp>

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
      : ModelingMementoCommand(modeling, std::move(label), false, false,
                               tool_id == "merge_edges" || tool_id == "merge_by_distance" ||
                               tool_id == "merge_vertices_to_active" ||
                               tool_id == "merge_vertices_to_center"),
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
      : ModelingMementoCommand(modeling, std::move(label), false, false,
                               action_id == "merge_edges" || action_id == "merge_by_distance" ||
                               action_id == "merge_vertices" ||
                               action_id == "merge_vertices_at_center"),
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
    case ModelingViewportTool::Box: {
      const ModelingCreateBoxResult created = modeling().commit_box("Box");
      result = {.ok = created.ok, .changed = created.changed,
                .message = created.message};
      break;
    }
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

std::string modeling_history_label(std::string_view label,
                                   std::string_view action_id) {
  if (!label.empty()) {
    return std::string(label);
  }
  if (const auto *action =
          quader_host_contract::find_action_descriptor(action_id);
      action != nullptr && !action->label.empty()) {
    return std::string(action->label);
  }
  if (const auto *operation =
          quader_host_contract::find_operation_descriptor(action_id);
      operation != nullptr && !operation->label.empty()) {
    return std::string(operation->label);
  }
  if (const auto *tool =
          quader_host_contract::find_tool_descriptor(action_id);
      tool != nullptr && !tool->label.empty()) {
    return std::string(tool->label);
  }
  return "Modeling Operation";
}

std::string command_label(const CommandTool *tool, std::string_view tool_id) {
  return modeling_history_label(tool == nullptr ? std::string_view{}
                                                : tool->label(),
                                tool_id);
}

} // namespace

ModelingCommandResult ModelingCommandExecutionService::execute(
    EditorHistoryService &history, ModelingSessionService &modeling,
    EditorSelectionService &selection, std::string_view tool_id,
    SceneStructuralLifecycleService *structural_lifecycle) {
  if (!modeling.action_supports_editor_mode(tool_id)) {
    return {.ok = false, .changed = false,
            .message = "The action is unavailable in the current editor mode."};
  }
  if (!release_allows_action(tool_id) || !release_allows_operation(tool_id) ||
      !release_allows_tool(tool_id)) {
    return {.ok = false, .changed = false,
            .message = release_feature_unavailable_message()};
  }
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

  if (modeling.pending_operation_active()) {
    const ModelingCommandResult settled =
        settle_pending_operations(history, modeling);
    if (!settled.ok) {
      return settled;
    }
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
  if (!modeling.action_supports_editor_mode(tool_id)) {
    return {.ok = false, .changed = false,
            .message = "The action is unavailable in the current editor mode."};
  }
  if (!release_allows_action(tool_id) || !release_allows_operation(tool_id) ||
      !release_allows_tool(tool_id)) {
    return {.ok = false, .changed = false,
            .message = release_feature_unavailable_message()};
  }
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
  if (!modeling.action_supports_editor_mode(action_id)) {
    return {.ok = false, .changed = false,
            .message = "The action is unavailable in the current editor mode."};
  }
  if (!release_allows_action(action_id) || !release_allows_operation(action_id) ||
      !release_allows_tool(action_id)) {
    return {.ok = false, .changed = false,
            .message = release_feature_unavailable_message()};
  }
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
  if (found->visibility != ModelingActionVisibility::Enabled) {
    return {.ok = false,
            .message = found->disabled_reason.empty()
                           ? "Action is disabled."
                           : found->disabled_reason};
  }
  if (modeling.operation_preview_active() &&
      modeling.tool_settings_target().action_id == action_id) {
    return {.ok = true};
  }
  if (modeling.pending_operation_active()) {
    const ModelingCommandResult settled =
        settle_pending_operations(history, modeling);
    if (!settled.ok) {
      return settled;
    }
  }
  if (found->preview_kind != ModelingActionPreviewKind::None) {
    return modeling.execute_context_action(action_id);
  }
  return from_command_result(
      history.execute(std::make_unique<ModelingActionHistoryCommand>(
          modeling, std::string(action_id),
          modeling_history_label(found->label, action_id))));
}

ModelingCommandResult ModelingCommandExecutionService::begin_preview_untracked(
    ModelingSessionService &modeling, std::string_view action_id) {
  if (!release_allows_action(action_id) || !release_allows_operation(action_id) ||
      !release_allows_tool(action_id)) {
    return {.ok = false, .changed = false,
            .message = release_feature_unavailable_message()};
  }
  return modeling.execute_context_action(action_id);
}

ModelingCommandResult ModelingCommandExecutionService::commit_preview(
    EditorHistoryService &history, ModelingSessionService &modeling,
    std::string label) {
  if (label.empty()) {
    const ModelingToolSettingsTarget target = modeling.tool_settings_target();
    label = modeling_history_label(
        target.label,
        target.action_id.empty() ? target.operation_id : target.action_id);
  }
  return ModelingPreviewCommandService::commit(history, modeling,
                                               std::move(label));
}

ModelingCommandResult ModelingCommandExecutionService::cancel_preview(
    ModelingSessionService &modeling) {
  return ModelingPreviewCommandService::cancel(modeling);
}

ModelingCommandResult ModelingCommandExecutionService::settle_pending_operations(
    EditorHistoryService &history, ModelingSessionService &modeling,
    MaterialReference creation_material) {
  bool changed = false;
  const auto absorb = [&changed](ModelingCommandResult result) {
    changed = changed || result.changed;
    result.changed = changed;
    return result;
  };
  if (modeling.operation_preview_active()) {
    const ModelingCommandResult result = absorb(commit_preview(
        history, modeling, modeling.tool_settings_target().label));
    if (!result.ok) {
      return result;
    }
  }
  if (modeling.transform_drag_active()) {
    const ModelingTransformDragPreview result =
        modeling.commit_transform_drag(history);
    const ModelingCommandResult committed = absorb({
        .ok = result.ok, .changed = result.changed, .message = result.message});
    if (!committed.ok) {
      return committed;
    }
  }
  if (modeling.structural_transform_preview_active()) {
    const ModelingCommandResult result =
        absorb(modeling.commit_structural_transform_preview());
    if (!result.ok) {
      return result;
    }
  }
  if (modeling.can_cancel_active_preview_for_undo()) {
    const ModelingCommandResult result = absorb(from_command_result(
        history.execute(std::make_unique<CommitTransformPreviewHistoryCommand>(
            modeling))));
    if (!result.ok) {
      return result;
    }
  }

  const ModelingToolStatus status = modeling.tool_status();
  bool commit = false;
  switch (status.active_tool) {
  case ModelingViewportTool::InsertEdgeLoop:
    // Merely hovering a loop is not an authored gesture.
    commit = status.insert_edge_loop_dragging;
    break;
  case ModelingViewportTool::Knife:
    if (status.knife_has_active_stroke && !status.knife_can_commit) {
      return {.ok = false, .changed = changed,
              .message = "Finish the Knife stroke or explicitly cancel it "
                         "before continuing."};
    }
    commit = status.knife_can_commit;
    break;
  case ModelingViewportTool::Cut:
    commit = status.cut_captured_point_count != 0;
    break;
  case ModelingViewportTool::Mirror:
    commit = status.mirror_captured_point_count != 0;
    break;
  case ModelingViewportTool::Poly:
    commit = status.poly_point_count != 0;
    break;
  case ModelingViewportTool::Box:
    commit = modeling.pending_operation_active();
    break;
  default:
    break;
  }
  if (commit) {
    const ToolDescriptor *descriptor =
        find_viewport_tool_descriptor(status.active_tool);
    const ModelingCommandResult result = absorb(commit_viewport_tool(
        history, modeling, status.active_tool,
        descriptor == nullptr ? "Modeling Operation"
                              : std::string(descriptor->label),
        creation_material));
    if (!result.ok) {
      return result;
    }
  }
  if (modeling.pending_operation_active()) {
    return {.ok = false, .changed = changed,
            .message = "Finish the pending operation or explicitly cancel it "
                       "before continuing."};
  }
  if (modeling.modal_tool_owns_viewport()) {
    // Only empty startup/hover state remains; this cannot discard authored work.
    return absorb(modeling.cancel_active_modal_tool());
  }
  return {.ok = true, .changed = changed};
}

ModelingCommandResult ModelingCommandExecutionService::commit_viewport_tool(
    EditorHistoryService &history, ModelingSessionService &modeling,
    ModelingViewportTool tool, std::string label,
    MaterialReference creation_material) {
  const ToolDescriptor *descriptor = find_viewport_tool_descriptor(tool);
  if (descriptor != nullptr && !release_allows_tool(descriptor->id)) {
    return {.ok = false, .changed = false,
            .message = release_feature_unavailable_message()};
  }
  switch (tool) {
  case ModelingViewportTool::Box:
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
  if (!modeling.action_supports_editor_mode(tool_id)) {
    return ModelingCommandAvailability::unavailable("The action is unavailable in the current editor mode.");
  }
  if (!release_allows_action(tool_id) || !release_allows_operation(tool_id) ||
      !release_allows_tool(tool_id)) {
    return ModelingCommandAvailability::unavailable(
        release_feature_unavailable_message());
  }
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

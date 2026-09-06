////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/drag_commit_tool.hpp"

#include "editor/modeling/services/selection_mode_adapter.hpp"

namespace quader {
namespace {

void handle_result(ViewportToolContext &context, std::string_view label,
                   const ModelingCommandResult &command,
                   bool announce_failure = false) {
  if (context.ports.command_result != nullptr) {
    context.ports.command_result->handle_tool_command_result(
        label, command, announce_failure);
  }
}

} // namespace

ViewportToolResult DragCommitTool::handle_event(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
  if (event.kind != ViewportToolEventKind::Frame ||
      event.tool != viewport_tool() ||
      modeling_tool() != ModelingViewportTool::InsertEdgeLoop ||
      context.modal_state == nullptr || context.point == nullptr ||
      !context.point->make_event_at || context.ports.hover == nullptr ||
      context.ports.preview == nullptr || context.ports.commit == nullptr) {
    return ModalViewportTool::handle_event(context, event);
  }

  ViewportPointToolContext &point_context = *context.point;
  ViewportToolHoverPort &hover_port = *context.ports.hover;
  ViewportToolPreviewPort &preview_port = *context.ports.preview;
  ViewportToolCommitPort &commit_port = *context.ports.commit;
  const ViewportPointerToolEvent &pointer = event.pointer;
  const bool dragging =
      context.modal_state->insert_edge_loop_drag_active ||
      preview_port.tool_status().insert_edge_loop_dragging;

  if (pointer.can_interact && !pointer.left_down && !pointer.left_released &&
      !dragging && !pointer.suppress_hover_work &&
      hover_port.claim(ModelingBoxDragState::Idle)) {
    handle_result(context, "Insert edge loop hover",
                  hover_port.update_insert_edge_loop_hover(
                      point_context.make_event_at(pointer.mouse_local, false)));
  }

  if (pointer.can_interact && pointer.left_clicked) {
    const ModelingCommandResult begin_command =
        preview_port.begin_insert_edge_loop_drag(
            point_context.make_event_at(pointer.left_click_local, true));
    handle_result(context, "Insert edge loop begin", begin_command, true);
    const bool can_commit =
        begin_command.ok &&
        preview_port.tool_status().insert_edge_loop_dragging;
    context.modal_state->insert_edge_loop_drag_active = false;
    if (can_commit) {
      const ModelingCommandResult commit_command =
          commit_port.commit_insert_edge_loop_drag();
      handle_result(context, "Insert edge loop commit", commit_command, true);
      if (context.ports.selection_mode != nullptr) {
        context.ports.selection_mode->set_viewport_selection_mode(
            app_selection_mode_to_viewport(
                context.ports.selection_mode->selection_summary().mode));
      }
      return {
          .handled = true,
          .modal_state_changed = true,
          .active_tool_changed = commit_command.ok,
          .sync_active_tool_from_modeling = commit_command.ok,
      };
    }
  } else if (pointer.can_interact && pointer.left_down && dragging) {
    handle_result(context, "Insert edge loop drag update",
                  preview_port.update_insert_edge_loop_drag(
                      point_context.make_event_at(pointer.mouse_local, true)));
  }

  if (pointer.left_released && dragging) {
    const ModelingCommandResult command =
        commit_port.commit_insert_edge_loop_drag();
    handle_result(context, "Insert edge loop commit", command, true);
    context.modal_state->insert_edge_loop_drag_active = false;
    if (context.ports.selection_mode != nullptr) {
      context.ports.selection_mode->set_viewport_selection_mode(
          app_selection_mode_to_viewport(
              context.ports.selection_mode->selection_summary().mode));
    }
  }

  return {
      .handled = true,
      .modal_state_changed = true,
  };
}

} // namespace quader

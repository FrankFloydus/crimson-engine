////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/drag_commit_tool.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

namespace quader {
namespace {

void handle_result(ViewportToolContext &context, std::string_view label,
                   const ModelingCommandResult &command) {
  if (context.handle_tool_command_result) {
    context.handle_tool_command_result(label, command);
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
      !context.point->make_event_at) {
    return ModalViewportTool::handle_event(context, event);
  }

  ViewportPointToolContext &point_context = *context.point;
  const ViewportPointerToolEvent &pointer = event.pointer;
  const bool dragging =
      context.modal_state->insert_edge_loop_drag_active ||
      context.modeling.tool_status().insert_edge_loop_dragging;

  if (pointer.can_interact && !pointer.left_down && !pointer.left_released &&
      !dragging && !pointer.suppress_hover_work &&
      (!context.claim_hover ||
       context.claim_hover(ModelingBoxDragState::Idle))) {
    handle_result(context, "Insert edge loop hover",
                  context.modeling.update_insert_edge_loop_hover(
                      point_context.make_event_at(pointer.mouse_local, false)));
  }

  if (pointer.can_interact && pointer.left_clicked) {
    const ModelingCommandResult begin_command =
        context.modeling.begin_insert_edge_loop_drag(
            point_context.make_event_at(pointer.mouse_local, true));
    handle_result(context, "Insert edge loop begin", begin_command);
    const bool can_commit =
        begin_command.ok &&
        context.modeling.tool_status().insert_edge_loop_dragging;
    context.modal_state->insert_edge_loop_drag_active = false;
    if (can_commit) {
      const ModelingCommandResult commit_command =
          context.modeling.commit_insert_edge_loop_drag();
      handle_result(context, "Insert edge loop commit", commit_command);
      if (context.set_selection_mode) {
        context.set_selection_mode(app_selection_mode_to_viewport(
            context.modeling.selection_summary().mode));
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
                  context.modeling.update_insert_edge_loop_drag(
                      point_context.make_event_at(pointer.mouse_local, true)));
  }

  if (pointer.left_released && dragging) {
    const ModelingCommandResult command =
        context.modeling.commit_insert_edge_loop_drag();
    handle_result(context, "Insert edge loop commit", command);
    context.modal_state->insert_edge_loop_drag_active = false;
    if (context.set_selection_mode) {
      context.set_selection_mode(app_selection_mode_to_viewport(
          context.modeling.selection_summary().mode));
    }
  }

  return {
      .handled = true,
      .modal_state_changed = true,
  };
}

} // namespace quader

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
  const ModelingToolStatus status = preview_port.tool_status();
  const bool dragging =
      context.modal_state->insert_edge_loop_drag_active ||
      status.insert_edge_loop_dragging;
  const auto finish_drag = [&]() {
    const ModelingCommandResult command =
        commit_port.commit_insert_edge_loop_drag();
    handle_result(context, "Insert edge loop commit", command, true);
    if (command.ok) {
      context.modal_state->insert_edge_loop_drag_active = false;
      if (context.ports.selection_mode != nullptr) {
        context.ports.selection_mode->set_viewport_selection_mode(
            app_selection_mode_to_viewport(
                context.ports.selection_mode->selection_summary().mode));
      }
    }
    return command.ok;
  };
  const bool navigation_suspended =
      pointer.navigation_active && !pointer.pointer_interaction_canceled &&
      pointer.viewport_focused && !pointer.text_input_active &&
      !pointer.gizmo_consumed;
  if (navigation_suspended && !(dragging && pointer.left_released)) {
    // Camera navigation suspends picking and preserves the last preview. An
    // authored drag still accepts its last valid result on its own LMB release.
    return {.handled = true, .consumed = dragging};
  }
  const bool interrupted = pointer.pointer_interaction_canceled ||
      !pointer.viewport_focused || pointer.navigation_active ||
      pointer.text_input_active || pointer.gizmo_consumed ||
      (!dragging && !pointer.can_interact) ||
      (dragging && pointer.left_released && !pointer.can_interact);
  if (interrupted) {
    const bool focus_only = !pointer.viewport_focused &&
        !pointer.pointer_interaction_canceled && !pointer.navigation_active &&
        !pointer.text_input_active && !pointer.gizmo_consumed;
    if (dragging && focus_only && pointer.left_down && !pointer.left_released) {
      return {.handled = true, .consumed = true};
    }
    if (dragging) {
      const bool finished = finish_drag();
      if (finished) {
        context.modal_state->primary_release_suppression.begin(
            QuaderMouseButton::Left, pointer.left_down);
      }
      return {.handled = true, .consumed = true,
              .modal_state_changed = finished};
    }
    if (status.insert_edge_loop_has_preview) {
      // Hover alone has no authored insertion to accept.
      context.modal_state->primary_release_suppression.begin(
          QuaderMouseButton::Left, pointer.left_down);
      return cancel_active_modal_tool(context, false);
    }
    return {.handled = true};
  }

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
    if (can_commit) {
      const bool finished = finish_drag();
      return {
          .handled = true,
          .modal_state_changed = finished,
      };
    }
  } else if (pointer.can_interact && pointer.left_down && dragging) {
    handle_result(context, "Insert edge loop drag update",
                  preview_port.update_insert_edge_loop_drag(
                      point_context.make_event_at(pointer.mouse_local, true)));
  }

  if (dragging && (pointer.left_released ||
                   (!pointer.left_down && !pointer.left_clicked))) {
    const bool finished = finish_drag();
    return {
        .handled = true,
        .modal_state_changed = finished,
    };
  }

  return {
      .handled = true,
      .modal_state_changed = true,
  };
}

} // namespace quader

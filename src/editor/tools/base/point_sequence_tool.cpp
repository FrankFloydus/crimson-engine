////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/point_sequence_tool.hpp"

#include "editor/modeling/services/selection_mode_adapter.hpp"

namespace quader {
namespace {

void handle_result(ViewportToolContext &context, std::string_view label,
                   const ModelingCommandResult &command) {
  if (context.ports.command_result != nullptr) {
    context.ports.command_result->handle_tool_command_result(label, command);
  }
}

void finish_point_sequence_tool(ViewportToolContext &context) {
  if (context.ports.selection_mode != nullptr) {
    context.ports.selection_mode->set_viewport_selection_mode(
        app_selection_mode_to_viewport(
            context.ports.selection_mode->selection_summary().mode));
  }
}

} // namespace

ViewportToolResult PointSequenceTool::handle_event(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
  if (event.kind != ViewportToolEventKind::Frame ||
      event.tool != viewport_tool() || context.modal_state == nullptr) {
    return ModalViewportTool::handle_event(context, event);
  }

  ViewportPointToolContext *point_context = context.point;
  ViewportToolHoverPort *hover_port = context.ports.hover;
  ViewportToolPreviewPort *preview_port = context.ports.preview;
  ViewportToolCommitPort *commit_port = context.ports.commit;
  const ViewportPointerToolEvent &pointer = event.pointer;
  switch (modeling_tool()) {
  case ModelingViewportTool::Knife:
    if (point_context == nullptr || !point_context->make_event_at) {
      break;
    }
    if (pointer.can_interact && !pointer.suppress_hover_work &&
        hover_port != nullptr && hover_port->claim(ModelingBoxDragState::Idle)) {
      handle_result(
          context, "Knife hover",
          hover_port->update_knife_hover(
              point_context->make_event_at(pointer.mouse_local, false)));
    }
    if (pointer.can_interact && pointer.left_clicked &&
        preview_port != nullptr) {
      handle_result(
          context, "Knife point",
          preview_port->accept_knife_point(
              point_context->make_event_at(pointer.left_click_local, false)));
    }
    if (pointer.finalize_pressed && commit_port != nullptr) {
      const ModelingCommandResult command =
          commit_port->commit_knife_tool();
      handle_result(context, "Knife commit", command);
      if (command.ok) {
        finish_point_sequence_tool(context);
      }
      return {
          .handled = true,
          .active_tool_changed = command.ok,
          .sync_active_tool_from_modeling = command.ok,
      };
    }
    return {
        .handled = true,
    };
  case ModelingViewportTool::Poly:
    if (pointer.can_interact && !pointer.suppress_hover_work &&
        point_context != nullptr && point_context->make_event_at &&
        hover_port != nullptr && hover_port->claim(ModelingBoxDragState::Idle)) {
      handle_result(
          context, "Poly hover",
          hover_port->update_poly_hover(
              point_context->make_event_at(pointer.mouse_local, false)));
    }
    if (pointer.can_interact && pointer.left_clicked &&
        point_context != nullptr && point_context->make_event_at &&
        preview_port != nullptr) {
      handle_result(
          context, "Poly point",
          preview_port->accept_poly_point(
              point_context->make_event_at(pointer.left_click_local, false)));
    }
    if (pointer.finalize_pressed && commit_port != nullptr) {
      const ModelingCommandResult command = commit_port->commit_poly_tool();
      handle_result(context, "Poly commit", command);
      if (command.ok) {
        finish_point_sequence_tool(context);
      }
      return {
          .handled = true,
          .active_tool_changed = command.ok,
          .sync_active_tool_from_modeling = command.ok,
      };
    }
    return {
        .handled = true,
    };
  case ModelingViewportTool::Select:
  case ModelingViewportTool::Move:
  case ModelingViewportTool::Extrude:
  case ModelingViewportTool::Rotate:
  case ModelingViewportTool::Scale:
  case ModelingViewportTool::Box:
  case ModelingViewportTool::Cylinder:
  case ModelingViewportTool::Sphere:
  case ModelingViewportTool::InsertEdgeLoop:
  case ModelingViewportTool::Cut:
  case ModelingViewportTool::Mirror:
    break;
  }
  return ModalViewportTool::handle_event(context, event);
}

} // namespace quader

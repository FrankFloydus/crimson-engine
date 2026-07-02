////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/point_sequence_tool.hpp"

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

void finish_point_sequence_tool(ViewportToolContext &context) {
  if (context.set_selection_mode) {
    context.set_selection_mode(app_selection_mode_to_viewport(
        context.modeling.selection_summary().mode));
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
  const ViewportPointerToolEvent &pointer = event.pointer;
  switch (modeling_tool()) {
  case ModelingViewportTool::Knife:
    if (point_context == nullptr || !point_context->make_event_at) {
      break;
    }
    if (pointer.can_interact && !pointer.suppress_hover_work &&
        (!context.claim_hover ||
         context.claim_hover(ModelingBoxDragState::Idle))) {
      handle_result(
          context, "Knife hover",
          context.modeling.update_knife_hover(
              point_context->make_event_at(pointer.mouse_local, false)));
    }
    if (pointer.can_interact && pointer.left_clicked) {
      handle_result(
          context, "Knife point",
          context.modeling.accept_knife_point(
              point_context->make_event_at(pointer.mouse_local, false)));
    }
    if (pointer.finalize_pressed) {
      const ModelingCommandResult command =
          context.modeling.commit_knife_tool();
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
        (!context.claim_hover ||
         context.claim_hover(ModelingBoxDragState::Idle))) {
      handle_result(
          context, "Poly hover",
          context.modeling.update_poly_hover(
              point_context->make_event_at(pointer.mouse_local, false)));
    }
    if (pointer.can_interact && pointer.left_clicked) {
      handle_result(context, "Poly point",
                    context.modeling.accept_poly_point());
    }
    if (pointer.finalize_pressed) {
      const ModelingCommandResult command = context.modeling.commit_poly_tool();
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
  case ModelingViewportTool::Extend:
  case ModelingViewportTool::Extrude:
  case ModelingViewportTool::Rotate:
  case ModelingViewportTool::Scale:
  case ModelingViewportTool::Box:
  case ModelingViewportTool::InsertEdgeLoop:
  case ModelingViewportTool::Cut:
  case ModelingViewportTool::Mirror:
    break;
  }
  return ModalViewportTool::handle_event(context, event);
}

} // namespace quader

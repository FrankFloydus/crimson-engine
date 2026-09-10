////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/modal_viewport_tool.hpp"

#include "core/logger.hpp"

namespace quader {

ViewportToolResult ModalViewportTool::handle_event(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind == ViewportToolEventKind::Activation) {
      return activate_by_id(context, event.tool);
    }
    if (event.kind == ViewportToolEventKind::Cancel) {
      return cancel_active_modal_tool(context, event.exit_active_modal_tool);
    }
    return {};
}

ViewportToolResult
ModalViewportTool::cancel_active_modal_tool(ViewportToolContext &context,
                                            bool exit_tool) const {
  if (context.ports.cancel == nullptr) {
    return {};
  }
  const ModelingViewportTool active_before = context.ports.preview != nullptr
      ? context.ports.preview->tool_status().active_tool : modeling_tool();
  const ModelingCommandResult cancel =
      context.ports.cancel->cancel_active_modal_tool();
  const bool active_tool_changed = exit_tool && context.ports.preview != nullptr &&
      context.ports.preview->tool_status().active_tool != active_before;
  if (!cancel.ok) {
    if (context.ports.command_result != nullptr) {
      context.ports.command_result->handle_tool_command_result(
          "Modal tool cancel", cancel, true);
    } else {
      LOG_ERROR("Modal tool cancel failed: {}", cancel.message);
    }
  }
  if (context.modal_state != nullptr) {
    context.modal_state->insert_edge_loop_drag_active = false;
    context.modal_state->cut_drag_active = false;
    context.modal_state->mirror_drag_active = false;
  }
  if (context.ports.hover != nullptr) {
    context.ports.hover->invalidate();
  }
  if (context.ports.command_result != nullptr) {
    context.ports.command_result->mark_overlay_dirty();
  }
  return {
      .handled = true,
      .consumed = true,
      .modal_state_changed = true,
      .active_tool_changed = active_tool_changed,
      .sync_active_tool_from_modeling = active_tool_changed,
  };
}

} // namespace quader

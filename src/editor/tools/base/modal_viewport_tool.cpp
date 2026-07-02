////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/modal_viewport_tool.hpp"

#include "core/logger.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

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
  const ModelingCommandResult cancel =
      context.modeling.cancel_active_modal_tool();
  if (!cancel.ok) {
    LOG_ERROR("Modal tool cancel failed: {}", cancel.message);
  }
  if (context.modal_state != nullptr) {
    context.modal_state->insert_edge_loop_drag_active = false;
    context.modal_state->cut_drag_active = false;
    context.modal_state->mirror_drag_active = false;
  }
  if (context.invalidate_hover) {
    context.invalidate_hover();
  }
  if (context.mark_overlay_dirty) {
    context.mark_overlay_dirty();
  }
  return {
      .handled = true,
      .consumed = true,
      .modal_state_changed = true,
      .active_tool_changed = exit_tool,
      .sync_active_tool_from_modeling = exit_tool,
  };
}

} // namespace quader

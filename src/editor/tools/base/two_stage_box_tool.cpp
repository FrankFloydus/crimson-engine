////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/two_stage_box_tool.hpp"

#include "core/logger.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <utility>

namespace quader {
namespace {

void invalidate(ViewportToolContext& context) {
  if (context.invalidate_hover) {
    context.invalidate_hover();
  }
  if (context.mark_overlay_dirty) {
    context.mark_overlay_dirty();
  }
}

} // namespace

bool TwoStageBoxTool::has_active_preview(
    const ModelingBoxPreview &preview) const {
  return preview.valid || preview.drag_state != ModelingBoxDragState::Idle;
}

void TwoStageBoxTool::log_preview_failure(
    const char *action, const ModelingBoxPreview &preview) const {
  if (!preview.ok) {
    LOG_ERROR("{} failed: {}", action,
              preview.message.empty() ? "Box preview command failed."
                                      : preview.message);
  }
}

bool TwoStageBoxTool::commit_preview(ViewportBoxToolContext &box_context,
                                     const std::string &name) const {
  if (!box_context.commit_preview) {
    LOG_ERROR("Box commit failed: Commit command is not available.");
    return false;
  }

  const ModelingCreateBoxResult committed =
      box_context.commit_preview(name, box_context.default_material);
  if (!committed.ok) {
    LOG_ERROR("Box commit failed: {}", committed.message);
    return false;
  }
  return committed.changed;
}

ViewportToolResult TwoStageBoxTool::handle_event(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind == ViewportToolEventKind::Cancel) {
      if (context.modal_state == nullptr) {
        return ModalViewportTool::handle_event(context, event);
      }
      const ModelingCommandResult cancel = context.modeling.cancel_box();
      if (!cancel.ok) {
        LOG_ERROR("Box cancel failed: {}", cancel.message);
      }
      context.modal_state->box_preview = {};
      context.modal_state->insert_edge_loop_drag_active = false;
      context.modal_state->cut_drag_active = false;
      context.modal_state->mirror_drag_active = false;
      invalidate(context);
      return {
          .handled = true,
          .consumed = true,
          .modal_state_changed = true,
          .active_tool_changed = event.exit_active_modal_tool,
          .sync_active_tool_from_modeling = event.exit_active_modal_tool,
      };
    }

    if (event.kind != ViewportToolEventKind::Frame ||
        event.tool != viewport_tool() || context.modal_state == nullptr ||
        context.box == nullptr || !context.box->make_event ||
        !context.box->commit_preview) {
      return ModalViewportTool::handle_event(context, event);
    }

    ViewportBoxToolContext &box_context = *context.box;
    ModelingBoxPreview &preview = context.modal_state->box_preview;
    const ViewportPointerToolEvent& pointer = event.pointer;
    const ModelingBoxToolEvent box_event = box_context.make_event();
    bool action_consumed = false;
    bool active_tool_sync_requested = false;

    auto finish_footprint_and_commit = [&]() {
      preview = context.modeling.update_box_footprint(box_event);
      log_preview_failure("Box footprint finalize update", preview);
      if (preview.ok) {
        preview = context.modeling.begin_box_height(box_event);
        log_preview_failure("Box height begin", preview);
      }
      if (preview.ok) {
        const std::string name = box_context.next_mesh_name
                                     ? box_context.next_mesh_name()
                                     : std::string{"Mesh"};
        if (commit_preview(box_context, name)) {
          if (context.set_selection_mode) {
            context.set_selection_mode(ViewportSelectionMode::Object);
          }
          if (context.set_app_selection_mode) {
            context.set_app_selection_mode(ModelingSelectionMode::Object);
          }
          active_tool_sync_requested = true;
        }
      }
      preview = {};
      invalidate(context);
      action_consumed = true;
    };

    if (pointer.hovered && !pointer.left_down && !pointer.suppress_hover_work &&
        preview.drag_state == ModelingBoxDragState::Idle &&
        (!context.claim_hover || context.claim_hover(preview.drag_state))) {
      preview = context.modeling.update_box_hover(box_event);
      log_preview_failure("Box hover", preview);
    }

    if (pointer.left_clicked && !pointer.gizmo_consumed &&
        !pointer.navigation_active) {
      if (preview.drag_state == ModelingBoxDragState::Footprint) {
        finish_footprint_and_commit();
      } else {
        preview = context.modeling.begin_box_footprint(box_event);
        log_preview_failure("Box footprint begin", preview);
        invalidate(context);
        action_consumed = true;
      }
    }

    if (!action_consumed && pointer.left_released &&
        preview.drag_state == ModelingBoxDragState::Footprint &&
        !pointer.gizmo_consumed && !pointer.navigation_active) {
      finish_footprint_and_commit();
    }

    if (!action_consumed &&
        preview.drag_state == ModelingBoxDragState::Footprint &&
        pointer.hovered && !pointer.suppress_hover_work &&
        !pointer.navigation_active) {
      preview = context.modeling.update_box_footprint(box_event);
      log_preview_failure("Box footprint update", preview);
      invalidate(context);
    }

    return {
        .handled = true,
        .consumed = action_consumed,
        .modal_state_changed = true,
        .active_tool_changed = active_tool_sync_requested,
        .sync_active_tool_from_modeling = active_tool_sync_requested,
    };
}

} // namespace quader

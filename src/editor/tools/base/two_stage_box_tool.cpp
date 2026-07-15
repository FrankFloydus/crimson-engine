////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/two_stage_box_tool.hpp"

#include "core/logger.hpp"

#include <utility>

namespace quader {
namespace {

void invalidate(ViewportToolContext& context) {
  if (context.ports.hover != nullptr) {
    context.ports.hover->invalidate();
  }
  if (context.ports.command_result != nullptr) {
    context.ports.command_result->mark_overlay_dirty();
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

bool TwoStageBoxTool::accept_preview(ViewportBoxToolContext &box_context,
                                     const std::string &name,
                                     bool snap_enabled) const {
  if (!box_context.accept_preview) {
    LOG_ERROR("Primitive accept failed: Accept command is not available.");
    return false;
  }

  const ModelingCommandResult accepted =
      box_context.accept_preview(name, box_context.default_material,
                                 snap_enabled);
  if (!accepted.ok) {
    LOG_ERROR("Primitive accept failed: {}", accepted.message);
    return false;
  }
  return accepted.changed;
}

bool TwoStageBoxTool::rollback_failed_preview(
    ViewportBoxToolContext &box_context, const char *action,
    const ModelingBoxPreview &preview) const {
  log_preview_failure(action, preview);
  if (preview.ok || !box_context.cancel_session) {
    return false;
  }

  const ModelingCommandResult canceled = box_context.cancel_session();
  if (!canceled.ok) {
    LOG_ERROR("Primitive rollback failed: {}", canceled.message);
  }
  return canceled.ok ||
         (box_context.session_active && !box_context.session_active());
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
      if ((context.box == nullptr || !context.box->cancel_session) &&
          context.ports.cancel == nullptr) {
        return ModalViewportTool::handle_event(context, event);
      }
      const ModelingCommandResult cancel =
          context.box != nullptr && context.box->cancel_session
              ? context.box->cancel_session()
              : context.ports.cancel->cancel_box();
      if (!cancel.ok) {
        LOG_ERROR("Primitive cancel failed: {}", cancel.message);
      }
      const bool session_ended =
          context.box != nullptr && context.box->session_active &&
          !context.box->session_active();
      const bool sync_active_tool =
          event.exit_active_modal_tool && (cancel.ok || session_ended);
      context.modal_state->insert_edge_loop_drag_active = false;
      context.modal_state->cut_drag_active = false;
      context.modal_state->mirror_drag_active = false;
      invalidate(context);
      return {
          .handled = true,
          .consumed = true,
          .modal_state_changed = true,
          .active_tool_changed = sync_active_tool,
          .sync_active_tool_from_modeling = sync_active_tool,
      };
    }

    if (event.kind != ViewportToolEventKind::Frame ||
        event.tool != viewport_tool() || context.modal_state == nullptr ||
        context.box == nullptr || !context.box->make_event ||
        (!context.box->accept_preview && !context.box->commit_preview) ||
        context.ports.preview == nullptr) {
      return ModalViewportTool::handle_event(context, event);
    }

    ViewportBoxToolContext &box_context = *context.box;
    ViewportToolPreviewPort &preview_port = *context.ports.preview;
    const ViewportPointerToolEvent& pointer = event.pointer;
    const ModelingBoxToolEvent box_event = box_context.make_event();
    ModelingBoxPreview preview = preview_port.box_preview(box_event.snap_enabled);
    bool action_consumed = false;
    bool active_tool_sync_requested = false;

    if (!preview.ok) {
      active_tool_sync_requested = rollback_failed_preview(
          box_context, "Box preview query", preview);
      invalidate(context);
      return {
          .handled = true,
          .consumed = true,
          .modal_state_changed = true,
          .active_tool_changed = active_tool_sync_requested,
          .sync_active_tool_from_modeling = active_tool_sync_requested,
      };
    }

    if (pointer.finalize_pressed && box_context.apply_session) {
      const ModelingCommandResult applied = box_context.apply_session();
      if (!applied.ok) {
        LOG_ERROR("Primitive apply failed: {}", applied.message);
        active_tool_sync_requested =
            box_context.session_active && !box_context.session_active();
      } else {
        active_tool_sync_requested = true;
      }
      preview = {};
      invalidate(context);
      action_consumed = true;
    }

    auto finish_footprint_and_accept = [&]() {
      preview = preview_port.update_box_footprint(box_event);
      if (rollback_failed_preview(box_context,
                                  "Box footprint finalize update", preview)) {
        active_tool_sync_requested = true;
      }
      if (preview.ok) {
        preview = preview_port.begin_box_height(box_event);
        if (rollback_failed_preview(box_context, "Box height begin",
                                    preview)) {
          active_tool_sync_requested = true;
        }
      }
      if (preview.ok) {
        const std::string name = box_context.next_mesh_name
                                     ? box_context.next_mesh_name()
                                     : std::string{"Mesh"};
        if (box_context.accept_preview) {
          const bool accepted =
              accept_preview(box_context, name, box_event.snap_enabled);
          if (!accepted && box_context.session_active &&
              !box_context.session_active()) {
            active_tool_sync_requested = true;
          }
        } else if (commit_preview(box_context, name)) {
          if (context.ports.selection_mode != nullptr) {
            context.ports.selection_mode->set_viewport_selection_mode(
                ViewportSelectionMode::Object);
            context.ports.selection_mode->set_app_selection_mode(
                ModelingSelectionMode::Object);
          }
          active_tool_sync_requested = true;
        }
      }
      preview = {};
      invalidate(context);
      action_consumed = true;
    };

    if (!action_consumed && pointer.hovered && !pointer.left_down &&
        !pointer.suppress_hover_work &&
        preview.drag_state == ModelingBoxDragState::Idle &&
        (context.ports.hover == nullptr ||
         context.ports.hover->claim(preview.drag_state))) {
      preview = context.ports.hover != nullptr
                    ? context.ports.hover->update_box_hover(box_event)
                    : ModelingBoxPreview{};
      if (!preview.ok) {
        active_tool_sync_requested =
            rollback_failed_preview(box_context, "Box hover", preview);
        invalidate(context);
        action_consumed = true;
      }
    }

    if (!action_consumed && pointer.left_clicked && !pointer.gizmo_consumed &&
        !pointer.navigation_active) {
      if (preview.drag_state == ModelingBoxDragState::Footprint) {
        finish_footprint_and_accept();
      } else {
        preview = preview_port.begin_box_footprint(box_event);
        if (rollback_failed_preview(box_context, "Box footprint begin",
                                    preview)) {
          active_tool_sync_requested = true;
        }
        invalidate(context);
        action_consumed = true;
      }
    }

    if (!action_consumed && pointer.left_released &&
        preview.drag_state == ModelingBoxDragState::Footprint &&
        !pointer.gizmo_consumed && !pointer.navigation_active) {
      finish_footprint_and_accept();
    }

    if (!action_consumed &&
        preview.drag_state == ModelingBoxDragState::Footprint &&
        pointer.hovered && !pointer.suppress_hover_work &&
        !pointer.navigation_active) {
      preview = preview_port.update_box_footprint(box_event);
      if (rollback_failed_preview(box_context, "Box footprint update",
                                  preview)) {
        active_tool_sync_requested = true;
      }
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

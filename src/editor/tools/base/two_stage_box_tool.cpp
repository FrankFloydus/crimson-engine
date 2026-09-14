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

ViewportPointerCursor TwoStageBoxTool::pointer_cursor() const {
  // The same precision target remains meaningful before, during, and after a
  // footprint drag. Navigation and focus policy temporarily override it at
  // the host boundary without making the tool aware of Qt cursors.
  return ViewportPointerCursor::PrecisionCross;
}

ViewportToolResult TwoStageBoxTool::handle_event(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind == ViewportToolEventKind::Cancel) {
      if (context.modal_state == nullptr) {
        return ModalViewportTool::handle_event(context, event);
      }
      if (context.modal_state->primary_pointer_gesture) {
        context.modal_state->primary_release_suppression.begin(
            QuaderMouseButton::Left, true);
        context.modal_state->primary_pointer_gesture.reset();
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
        context.box == nullptr || !context.box->make_event_at ||
        !context.box->accept_preview ||
        context.ports.preview == nullptr) {
      return ModalViewportTool::handle_event(context, event);
    }

    ViewportBoxToolContext &box_context = *context.box;
    ViewportToolPreviewPort &preview_port = *context.ports.preview;
    const ViewportPointerToolEvent& pointer = event.pointer;
    ViewportModalToolState &modal_state = *context.modal_state;
    const bool canceled_release_reserved =
        modal_state.primary_release_suppression.consume(
            QuaderMouseButton::Left, pointer.left_clicked, pointer.left_down,
            pointer.left_released);
    if (canceled_release_reserved) {
      return {
          .handled = true,
          .consumed = true,
          .modal_state_changed = true,
      };
    }

    const auto make_box_event = [&](QVec2 local) {
      return box_context.make_event_at(local);
    };
    ModelingBoxToolEvent box_event = make_box_event(pointer.mouse_local);
    ModelingBoxPreview preview =
        preview_port.box_preview(box_event.snap_enabled);
    bool action_consumed = false;
    bool active_tool_sync_requested = false;
    bool settings_operation_started = false;

    if (preview.ok && preview.drag_state == ModelingBoxDragState::Idle &&
        modal_state.primary_pointer_gesture) {
      // Public history can reset the footprint while the same creation tool
      // stays active. Retire our old pointer gesture without canceling the
      // already-reset owner, and keep its held/release edges reserved.
      modal_state.primary_release_suppression.begin(
          QuaderMouseButton::Left, true);
      modal_state.primary_pointer_gesture.reset();
      invalidate(context);
      if (modal_state.primary_release_suppression.consume(
              QuaderMouseButton::Left, pointer.left_clicked,
              pointer.left_down, pointer.left_released)) {
        return {
            .handled = true,
            .consumed = true,
            .modal_state_changed = true,
        };
      }
    }

    const auto cancel_pointer_gesture = [&]() {
      if (!modal_state.primary_pointer_gesture) {
        return true;
      }
      ModelingCommandResult canceled =
          context.ports.cancel != nullptr
              ? context.ports.cancel->cancel_box()
              : ModelingCommandResult{
                    .ok = false,
                    .message = "Box cancel command is not available.",
                };
      if (!canceled.ok) {
        LOG_ERROR("Primitive pointer cancel failed: {}", canceled.message);
        return false;
      }
      modal_state.primary_release_suppression.begin(
          QuaderMouseButton::Left, pointer.left_down);
      modal_state.primary_pointer_gesture.reset();
      invalidate(context);
      return true;
    };

    const bool pointer_interrupted =
        modal_state.primary_pointer_gesture &&
        (pointer.pointer_interaction_canceled || !pointer.viewport_focused ||
         pointer.navigation_active || pointer.gizmo_consumed ||
         pointer.text_input_active);
    const bool pointer_abandoned =
        modal_state.primary_pointer_gesture && !pointer.left_down &&
        !pointer.left_released;
    if (pointer_interrupted || pointer_abandoned) {
      const bool canceled = cancel_pointer_gesture();
      return {
          .handled = true,
          .consumed = true,
          .modal_state_changed = canceled,
      };
    }

    if (!preview.ok) {
      if (modal_state.primary_pointer_gesture) {
        modal_state.primary_release_suppression.begin(
            QuaderMouseButton::Left, pointer.left_down);
      }
      modal_state.primary_pointer_gesture.reset();
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
      if (modal_state.primary_pointer_gesture) {
        modal_state.primary_release_suppression.begin(
            QuaderMouseButton::Left, pointer.left_down);
        modal_state.primary_pointer_gesture.reset();
      }
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

    auto finish_footprint_and_accept = [&](const ModelingBoxToolEvent &finish) {
      preview = preview_port.update_box_footprint(finish);
      if (rollback_failed_preview(box_context,
                                  "Box footprint finalize update", preview)) {
        active_tool_sync_requested = true;
      }
      if (preview.ok && !preview.valid) {
        // A zero-area footprint is still useful draw feedback, but it cannot
        // enter the height/commit phase. Retire only this pointer gesture so
        // the primitive session stays ready for another footprint.
        (void)cancel_pointer_gesture();
        preview = {};
        action_consumed = true;
        return;
      }
      if (!preview.ok) {
        modal_state.primary_release_suppression.begin(
            QuaderMouseButton::Left, pointer.left_down);
      }
      modal_state.primary_pointer_gesture.reset();
      if (preview.ok) {
        preview = preview_port.begin_box_height(finish);
        if (rollback_failed_preview(box_context, "Box height begin",
                                    preview)) {
          active_tool_sync_requested = true;
        }
      }
      if (preview.ok) {
        const std::string name = box_context.next_mesh_name
                                     ? box_context.next_mesh_name()
                                     : std::string{"Mesh"};
        const bool accepted =
            accept_preview(box_context, name, finish.snap_enabled);
        if (!accepted && box_context.session_active &&
            !box_context.session_active()) {
          active_tool_sync_requested = true;
        }
      }
      preview = {};
      invalidate(context);
      action_consumed = true;
    };

    if (!action_consumed && pointer.hovered && !pointer.left_down &&
        !modal_state.primary_pointer_gesture &&
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

    if (!action_consumed && !modal_state.primary_pointer_gesture &&
        pointer.left_clicked && !pointer.gizmo_consumed &&
        !pointer.navigation_active && pointer.viewport_focused &&
        !pointer.pointer_interaction_canceled) {
      modal_state.primary_pointer_gesture =
          viewport_pointer_gesture_from_latest_edge(
              pointer.pointer_pressed_edges, QuaderMouseButton::Left,
              pointer.left_click_local, pointer.mouse_screen, pointer.modifiers,
              pointer.pointer_drag_threshold_pixels);
      box_event =
          make_box_event(modal_state.primary_pointer_gesture->press.local);
      preview = preview_port.begin_box_footprint(box_event);
      settings_operation_started =
          preview.ok && preview.drag_state == ModelingBoxDragState::Footprint;
      if (rollback_failed_preview(box_context, "Box footprint begin",
                                  preview)) {
        active_tool_sync_requested = true;
        modal_state.primary_release_suppression.begin(
            QuaderMouseButton::Left, true);
        modal_state.primary_pointer_gesture.reset();
      }
      invalidate(context);
      action_consumed = true;
    }

    const ViewportPointerButtonEdge *release =
        modal_state.primary_pointer_gesture
            ? viewport_pointer_edge_after(
                  pointer.pointer_released_edges,
                  modal_state.primary_pointer_gesture->press)
            : nullptr;
    const bool matching_release =
        modal_state.primary_pointer_gesture && pointer.left_released &&
        (release != nullptr ||
         (modal_state.primary_pointer_gesture->press.sequence == 0u &&
          pointer.pointer_released_edges.empty()));
    if (!action_consumed && matching_release &&
        preview.drag_state == ModelingBoxDragState::Footprint &&
        !pointer.gizmo_consumed && !pointer.navigation_active) {
      const QVec2 release_local =
          release != nullptr ? release->local : pointer.mouse_local;
      if (viewport_pointer_drag_threshold_reached(
              *modal_state.primary_pointer_gesture, release_local)) {
        box_event = make_box_event(release_local);
        finish_footprint_and_accept(box_event);
      } else {
        (void)cancel_pointer_gesture();
        action_consumed = true;
      }
    }

    if (!action_consumed &&
        modal_state.primary_pointer_gesture && pointer.left_down &&
        preview.drag_state == ModelingBoxDragState::Footprint &&
        pointer.hovered && !pointer.suppress_hover_work &&
        !pointer.navigation_active &&
        viewport_pointer_drag_threshold_reached(
            *modal_state.primary_pointer_gesture, pointer.mouse_local)) {
      box_event = make_box_event(pointer.mouse_local);
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
        .settings_operation_started = settings_operation_started,
    };
}

} // namespace quader

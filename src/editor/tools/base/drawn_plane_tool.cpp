////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/drawn_plane_tool.hpp"

#include "core/logger.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace quader {
namespace {

void handle_result(ViewportToolContext &context, std::string_view label,
                   const ModelingCommandResult &command) {
  if (context.ports.command_result != nullptr) {
    context.ports.command_result->handle_tool_command_result(label, command);
  }
}

void finish_drawn_plane_tool(ViewportToolContext &context) {
  if (context.ports.selection_mode != nullptr) {
    context.ports.selection_mode->set_viewport_selection_mode(
        app_selection_mode_to_viewport(
            context.ports.selection_mode->selection_summary().mode));
  }
}

/**
 * Distinguishes drawn-plane tools that share the same viewport lifecycle.
 */
enum class DrawnPlaneToolKind {
    Cut,
    Mirror,
};

DrawnPlaneToolKind drawn_plane_tool_kind(ModelingViewportTool tool) {
  return tool == ModelingViewportTool::Cut ? DrawnPlaneToolKind::Cut
                                           : DrawnPlaneToolKind::Mirror;
}

std::string_view drawn_plane_tool_name(DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? "Cut" : "Mirror";
}

bool &drawn_plane_drag_active(ViewportModalToolState &state,
                              DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? state.cut_drag_active
                                         : state.mirror_drag_active;
}

bool drawn_plane_dragging(const ModelingToolStatus &status,
                          DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? status.cut_dragging
                                         : status.mirror_dragging;
}

bool drawn_plane_can_preview(const ModelingToolStatus &status,
                             DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? status.cut_can_preview
                                         : status.mirror_can_preview;
}

bool drawn_plane_preview_valid(const ModelingToolStatus &status,
                               DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? status.cut_preview_valid
                                         : status.mirror_preview_valid;
}

const std::array<QVec3, 3> &drawn_plane_points(const ModelingToolStatus &status,
                                               DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? status.cut_points
                                         : status.mirror_points;
}

int drawn_plane_captured_point_count(const ModelingToolStatus &status,
                                     DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? status.cut_captured_point_count
                                         : status.mirror_captured_point_count;
}

bool drawn_plane_has_line(const ModelingToolStatus &status,
                          DrawnPlaneToolKind kind) {
  return kind == DrawnPlaneToolKind::Cut ? status.cut_has_line
                                         : status.mirror_has_line;
}

ModelingCommandResult
update_drawn_plane_hover(ViewportToolHoverPort &hover,
                         DrawnPlaneToolKind kind,
                         const ModelingToolRayEvent &event) {
  return hover.update_drawn_plane_hover(
      kind == DrawnPlaneToolKind::Cut ? ModelingViewportTool::Cut
                                      : ModelingViewportTool::Mirror,
      event);
}

ModelingCommandResult
begin_drawn_plane_point_drag(ViewportToolPreviewPort &preview,
                             DrawnPlaneToolKind kind, int point_index) {
  return preview.begin_drawn_plane_point_drag(
      kind == DrawnPlaneToolKind::Cut ? ModelingViewportTool::Cut
                                      : ModelingViewportTool::Mirror,
      point_index);
}

ModelingCommandResult
begin_drawn_plane_line_drag(ViewportToolPreviewPort &preview,
                            DrawnPlaneToolKind kind,
                            const ModelingToolRayEvent &event) {
  return preview.begin_drawn_plane_line_drag(
      kind == DrawnPlaneToolKind::Cut ? ModelingViewportTool::Cut
                                      : ModelingViewportTool::Mirror,
      event);
}

ModelingCommandResult click_drawn_plane_point(
    ViewportToolPreviewPort &preview, DrawnPlaneToolKind kind,
    const ModelingToolRayEvent &event) {
  return preview.click_drawn_plane_point(
      kind == DrawnPlaneToolKind::Cut ? ModelingViewportTool::Cut
                                      : ModelingViewportTool::Mirror,
      event);
}

ModelingCommandResult end_drawn_plane_drag(ViewportToolPreviewPort &preview,
                                           DrawnPlaneToolKind kind) {
  return preview.end_drawn_plane_drag(
      kind == DrawnPlaneToolKind::Cut ? ModelingViewportTool::Cut
                                      : ModelingViewportTool::Mirror);
}

ModelingCommandResult
finalize_drawn_plane_tool(ViewportToolCommitPort &commit,
                          DrawnPlaneToolKind kind) {
  return commit.finalize_drawn_plane_tool(
      kind == DrawnPlaneToolKind::Cut ? ModelingViewportTool::Cut
                                      : ModelingViewportTool::Mirror);
}

void log_drawn_plane_commit_blocked(DrawnPlaneToolKind kind, bool dragging) {
  if (kind == DrawnPlaneToolKind::Cut) {
    if (dragging) {
      LOG_ERROR(
          "Cut commit failed: finish the current drag before committing.");
    } else {
      LOG_ERROR("Cut commit failed: place a valid cut line first.");
    }
    return;
  }
  if (dragging) {
    LOG_ERROR(
        "Mirror commit failed: finish the current drag before committing.");
  } else {
    LOG_ERROR("Mirror commit failed: place a valid mirror line first.");
  }
}

ViewportToolResult handle_drawn_plane_lifecycle(
    ViewportToolContext &context, const ViewportPointerToolEvent &pointer,
    ViewportPointToolContext &point_context, DrawnPlaneToolKind kind) {
  if (context.ports.hover == nullptr || context.ports.preview == nullptr ||
      context.ports.commit == nullptr) {
    return {};
  }
  ViewportToolHoverPort &hover_port = *context.ports.hover;
  ViewportToolPreviewPort &preview_port = *context.ports.preview;
  ViewportToolCommitPort &commit_port = *context.ports.commit;
  bool &drag_active = drawn_plane_drag_active(*context.modal_state, kind);
  const std::string_view tool_name = drawn_plane_tool_name(kind);

  if (pointer.can_interact && !pointer.suppress_hover_work &&
      !(pointer.left_down && drag_active) &&
      hover_port.claim(ModelingBoxDragState::Idle)) {
    handle_result(context, std::string(tool_name) + " hover",
                  update_drawn_plane_hover(hover_port, kind,
                                           point_context.make_event_at(
                                               pointer.mouse_local, false)));
  }
  if (pointer.can_interact && pointer.left_clicked) {
    const ModelingToolStatus before = preview_port.tool_status();
    ModelingCommandResult command;
    if (point_context.hit_drawn_plane_captured_point) {
      if (const std::optional<int> point_index =
              point_context.hit_drawn_plane_captured_point(
                  drawn_plane_points(before, kind),
                  drawn_plane_captured_point_count(before, kind))) {
        command =
            begin_drawn_plane_point_drag(preview_port, kind, *point_index);
        handle_result(context, std::string(tool_name) + " point drag begin",
                      command);
      }
    }
    if (!command.ok && command.message.empty() &&
        point_context.hit_drawn_plane_line &&
        point_context.hit_drawn_plane_line(
            drawn_plane_points(before, kind),
            drawn_plane_captured_point_count(before, kind),
            drawn_plane_has_line(before, kind))) {
      command = begin_drawn_plane_line_drag(
          preview_port, kind,
          point_context.make_event_at(pointer.left_click_local, true));
      handle_result(context, std::string(tool_name) + " line drag begin",
                    command);
    } else if (!command.ok && command.message.empty()) {
      command = click_drawn_plane_point(
          preview_port, kind,
          point_context.make_event_at(pointer.left_click_local, false));
      handle_result(context, std::string(tool_name) + " point", command);
    }
    drag_active = command.ok &&
                  drawn_plane_dragging(preview_port.tool_status(), kind);
  } else if (pointer.can_interact && pointer.left_down &&
             (drag_active ||
              drawn_plane_dragging(preview_port.tool_status(), kind))) {
    handle_result(context, std::string(tool_name) + " drag update",
                  update_drawn_plane_hover(hover_port, kind,
                                           point_context.make_event_at(
                                               pointer.mouse_local, true)));
    drag_active = drawn_plane_dragging(preview_port.tool_status(), kind);
  }
  if (pointer.left_released &&
      (drag_active ||
       drawn_plane_dragging(preview_port.tool_status(), kind))) {
    const ModelingCommandResult command =
        end_drawn_plane_drag(preview_port, kind);
    handle_result(context, std::string(tool_name) + " drag end", command);
    drag_active = false;
  }
  if (kind == DrawnPlaneToolKind::Cut && pointer.mode_cycle_pressed) {
    handle_result(context, "Cut mode", preview_port.cycle_cut_mode());
  }

  bool committed = false;
  if (pointer.finalize_pressed) {
    const ModelingToolStatus status = preview_port.tool_status();
    if (drawn_plane_dragging(status, kind)) {
      log_drawn_plane_commit_blocked(kind, true);
    } else if (!drawn_plane_can_preview(status, kind) ||
               !drawn_plane_preview_valid(status, kind)) {
      log_drawn_plane_commit_blocked(kind, false);
    } else {
      const ModelingCommandResult command =
          finalize_drawn_plane_tool(commit_port, kind);
      handle_result(context, std::string(tool_name) + " commit", command);
      if (command.ok) {
        finish_drawn_plane_tool(context);
        committed = true;
      }
    }
  }
  return {
      .handled = true,
      .modal_state_changed = true,
      .active_tool_changed = committed,
      .sync_active_tool_from_modeling = committed,
  };
}

} // namespace

ViewportToolResult DrawnPlaneTool::handle_event(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
  if (event.kind != ViewportToolEventKind::Frame ||
      event.tool != viewport_tool() || context.modal_state == nullptr ||
      context.point == nullptr || !context.point->make_event_at) {
    return ModalViewportTool::handle_event(context, event);
  }

  ViewportPointToolContext &point_context = *context.point;
  const ViewportPointerToolEvent &pointer = event.pointer;
  if (modeling_tool() == ModelingViewportTool::Cut ||
      modeling_tool() == ModelingViewportTool::Mirror) {
    return handle_drawn_plane_lifecycle(context, pointer, point_context,
                                        drawn_plane_tool_kind(modeling_tool()));
  }

  return ModalViewportTool::handle_event(context, event);
}

} // namespace quader

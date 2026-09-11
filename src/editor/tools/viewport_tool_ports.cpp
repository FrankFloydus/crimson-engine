////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/viewport_tool_ports.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"

#include <utility>

namespace quader {
namespace {

ModelingCommandResult unsupported_drawn_plane_tool() {
  return {
      .ok = false,
      .changed = false,
      .message = "Unsupported drawn-plane viewport tool.",
  };
}

} // namespace

ViewportModelingToolPortAdapter::ViewportModelingToolPortAdapter(
    ModelingSessionService &modeling,
    ViewportModelingToolPortCallbacks callbacks)
    : modeling_(modeling), callbacks_(std::move(callbacks)) {}

ViewportToolPorts
ViewportModelingToolPortAdapter::ports(ViewportToolCommitPort *commit) {
  return {
      .hover = this,
      .preview = this,
      .commit = commit,
      .cancel = this,
      .selection_mode = this,
      .command_result = this,
  };
}

bool ViewportModelingToolPortAdapter::claim(
    ModelingBoxDragState drag_state) {
  return callbacks_.claim_hover ? callbacks_.claim_hover(drag_state) : true;
}

void ViewportModelingToolPortAdapter::invalidate() {
  if (callbacks_.invalidate_hover) {
    callbacks_.invalidate_hover();
  }
}

ModelingBoxPreview ViewportModelingToolPortAdapter::update_box_hover(
    const ModelingBoxToolEvent &event) {
  return modeling_.update_box_hover(event);
}

ModelingCommandResult
ViewportModelingToolPortAdapter::update_insert_edge_loop_hover(
    const ModelingToolRayEvent &event) {
  return modeling_.update_insert_edge_loop_hover(event);
}

ModelingCommandResult ViewportModelingToolPortAdapter::update_knife_hover(
    const ModelingToolRayEvent &event) {
  return modeling_.update_knife_hover(event);
}

ModelingCommandResult ViewportModelingToolPortAdapter::clear_knife_hover() {
  return modeling_.clear_knife_hover();
}

ModelingCommandResult ViewportModelingToolPortAdapter::update_poly_hover(
    const ModelingToolRayEvent &event) {
  return modeling_.update_poly_hover(event);
}

ModelingCommandResult
ViewportModelingToolPortAdapter::update_drawn_plane_hover(
    ModelingViewportTool tool, const ModelingToolRayEvent &event) {
  if (tool == ModelingViewportTool::Cut) {
    return modeling_.update_cut_hover(event);
  }
  if (tool == ModelingViewportTool::Mirror) {
    return modeling_.update_mirror_hover(event);
  }
  return unsupported_drawn_plane_tool();
}

ModelingToolStatus ViewportModelingToolPortAdapter::tool_status() const {
  return modeling_.tool_status();
}

ModelingBoxPreview ViewportModelingToolPortAdapter::box_preview(
    bool snap_enabled) const {
  return modeling_.box_preview(snap_enabled);
}

ModelingBoxPreview ViewportModelingToolPortAdapter::begin_box_footprint(
    const ModelingBoxToolEvent &event) {
  return modeling_.begin_box_footprint(event);
}

ModelingBoxPreview ViewportModelingToolPortAdapter::update_box_footprint(
    const ModelingBoxToolEvent &event) {
  return modeling_.update_box_footprint(event);
}

ModelingBoxPreview ViewportModelingToolPortAdapter::begin_box_height(
    const ModelingBoxToolEvent &event) {
  return modeling_.begin_box_height(event);
}

ModelingCommandResult
ViewportModelingToolPortAdapter::begin_insert_edge_loop_drag(
    const ModelingToolRayEvent &event) {
  return modeling_.begin_insert_edge_loop_drag(event);
}

ModelingCommandResult
ViewportModelingToolPortAdapter::update_insert_edge_loop_drag(
    const ModelingToolRayEvent &event) {
  return modeling_.update_insert_edge_loop_drag(event);
}

ModelingCommandResult ViewportModelingToolPortAdapter::accept_knife_point(
    const ModelingToolRayEvent &event) {
  return modeling_.accept_knife_point(event);
}

ModelingCommandResult ViewportModelingToolPortAdapter::accept_poly_point(
    const ModelingToolRayEvent &event) {
  return modeling_.accept_poly_point(event);
}

ModelingCommandResult
ViewportModelingToolPortAdapter::begin_drawn_plane_point_drag(
    ModelingViewportTool tool, int point_index) {
  if (tool == ModelingViewportTool::Cut) {
    return modeling_.begin_cut_point_drag(point_index);
  }
  if (tool == ModelingViewportTool::Mirror) {
    return modeling_.begin_mirror_point_drag(point_index);
  }
  return unsupported_drawn_plane_tool();
}

ModelingCommandResult
ViewportModelingToolPortAdapter::begin_drawn_plane_line_drag(
    ModelingViewportTool tool, const ModelingToolRayEvent &event) {
  if (tool == ModelingViewportTool::Cut) {
    return modeling_.begin_cut_line_drag(event);
  }
  if (tool == ModelingViewportTool::Mirror) {
    return modeling_.begin_mirror_line_drag(event);
  }
  return unsupported_drawn_plane_tool();
}

ModelingCommandResult
ViewportModelingToolPortAdapter::click_drawn_plane_point(
    ModelingViewportTool tool, const ModelingToolRayEvent &event) {
  if (tool == ModelingViewportTool::Cut) {
    return modeling_.click_cut(event);
  }
  if (tool == ModelingViewportTool::Mirror) {
    return modeling_.click_mirror(event);
  }
  return unsupported_drawn_plane_tool();
}

ModelingCommandResult
ViewportModelingToolPortAdapter::end_drawn_plane_drag(
    ModelingViewportTool tool) {
  if (tool == ModelingViewportTool::Cut) {
    return modeling_.end_cut_drag();
  }
  if (tool == ModelingViewportTool::Mirror) {
    return modeling_.end_mirror_drag();
  }
  return unsupported_drawn_plane_tool();
}

ModelingCommandResult ViewportModelingToolPortAdapter::cycle_cut_mode() {
  return modeling_.cycle_cut_mode();
}

ModelingCommandResult ViewportModelingToolPortAdapter::cancel_box() {
  return modeling_.cancel_box();
}

ModelingCommandResult
ViewportModelingToolPortAdapter::cancel_active_modal_tool(bool exit_tool) {
  return modeling_.cancel_active_modal_tool(exit_tool);
}

ModelingSelectionSummary
ViewportModelingToolPortAdapter::selection_summary() const {
  return modeling_.selection_summary();
}

void ViewportModelingToolPortAdapter::set_viewport_selection_mode(
    ViewportSelectionMode mode) {
  if (callbacks_.set_selection_mode) {
    callbacks_.set_selection_mode(mode);
  }
}

void ViewportModelingToolPortAdapter::set_app_selection_mode(
    ModelingSelectionMode mode) {
  if (callbacks_.set_app_selection_mode) {
    callbacks_.set_app_selection_mode(mode);
  }
}

void ViewportModelingToolPortAdapter::handle_tool_command_result(
    std::string_view label, const ModelingCommandResult &result,
    bool announce_failure) {
  if (callbacks_.handle_tool_command_result) {
    callbacks_.handle_tool_command_result(label, result, announce_failure);
  }
}

void ViewportModelingToolPortAdapter::mark_overlay_dirty() {
  if (callbacks_.mark_overlay_dirty) {
    callbacks_.mark_overlay_dirty();
  }
}

} // namespace quader

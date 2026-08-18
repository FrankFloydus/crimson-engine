////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_hover_controller.hpp"

#include "viewport/viewport_camera_adapter.hpp"

namespace quader {
namespace {

bool vec3_matches(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

} // namespace

bool ViewportHoverWorkKey::matches(const ViewportHoverWorkKey& other) const {
  return valid && other.valid && pane_id == other.pane_id &&
         active_tool == other.active_tool &&
         selection_mode == other.selection_mode &&
         box_drag_state == other.box_drag_state &&
         grid_preset == other.grid_preset &&
         grid_snap_enabled == other.grid_snap_enabled &&
         mouse_local.x == other.mouse_local.x &&
         mouse_local.y == other.mouse_local.y &&
         viewport_size.x == other.viewport_size.x &&
         viewport_size.y == other.viewport_size.y &&
         vec3_matches(camera_eye, other.camera_eye) &&
         vec3_matches(camera_forward, other.camera_forward) &&
         vec3_matches(camera_right, other.camera_right) &&
         vec3_matches(camera_up, other.camera_up) &&
         camera_projection == other.camera_projection &&
         camera_orthographic_size == other.camera_orthographic_size &&
         camera_fov_degrees == other.camera_fov_degrees &&
         camera_near_plane == other.camera_near_plane &&
         camera_far_plane == other.camera_far_plane &&
         primary_modifier == other.primary_modifier &&
         shift_modifier == other.shift_modifier &&
         alt_modifier == other.alt_modifier &&
         gizmo_consumed == other.gizmo_consumed &&
         vertex_radius == other.vertex_radius &&
         edge_radius == other.edge_radius &&
         select_backfaces == other.select_backfaces &&
         scene_revision == other.scene_revision &&
         asset_revision == other.asset_revision &&
         local_visibility_revision == other.local_visibility_revision &&
         content_revision == other.content_revision &&
         selection_revision == other.selection_revision;
}

ViewportHoverWorkKey ViewportHoverController::make_key(
    const ViewportCameraController &camera,
    const ViewportHoverWorkRequest &request) const {
  const ViewportOverlayCamera overlay_camera =
      ViewportCameraAdapter::overlay_camera(camera, request.viewport_size);
  return {
      .valid = true,
      .pane_id = request.pane_id,
      .active_tool = request.active_tool,
      .selection_mode = request.selection_mode,
      .box_drag_state = request.box_drag_state,
      .grid_preset = request.grid_preset,
      .grid_snap_enabled = request.grid_snap_enabled,
      .mouse_local = request.mouse_local,
      .viewport_size = request.viewport_size,
      .camera_eye = overlay_camera.eye,
      .camera_forward = overlay_camera.forward,
      .camera_right = overlay_camera.right,
      .camera_up = overlay_camera.up,
      .camera_projection = overlay_camera.projection,
      .camera_orthographic_size = overlay_camera.orthographic_size,
      .camera_fov_degrees = overlay_camera.fov_degrees,
      .camera_near_plane = overlay_camera.near_plane,
      .camera_far_plane = overlay_camera.far_plane,
      .primary_modifier = request.modifiers.primary,
      .shift_modifier = request.modifiers.shift,
      .alt_modifier = request.modifiers.alt,
      .gizmo_consumed = request.gizmo_consumed,
      .vertex_radius = request.vertex_radius,
      .edge_radius = request.edge_radius,
      .select_backfaces = request.select_backfaces,
      .scene_revision = request.scene_revision,
      .asset_revision = request.asset_revision,
      .local_visibility_revision = request.local_visibility_revision,
      .content_revision = request.content_revision,
      .selection_revision = request.selection_revision,
  };
}

bool ViewportHoverController::claim(const ViewportHoverWorkKey& key) {
  if (last_key_.matches(key)) {
    return false;
  }
  last_key_ = key;
  return true;
}

void ViewportHoverController::invalidate() { last_key_.valid = false; }

} // namespace quader

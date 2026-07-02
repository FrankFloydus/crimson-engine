////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "modeling_test_events.hpp"

namespace quader::test {

ModelingViewportCamera orthographic_camera(CameraSpec spec) {
  return {
      .orthographic = true,
      .origin = spec.origin,
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = spec.viewport_size,
      .orthographic_size = spec.orthographic_size,
      .fov_degrees = 60.0,
  };
}

ModelingRay forward_ray(QVec3 origin) {
  return {
      .origin = origin,
      .direction = {0.0f, 0.0f, -1.0f},
  };
}

ModelingPickSelectRequest pick_at(QVec3 origin, PickOptions options) {
  return {
      .ray = forward_ray(origin),
      .click_count = options.click_count,
      .control_pressed = options.control_pressed,
      .shift_pressed = options.shift_pressed,
      .command_pressed = options.command_pressed,
      .alt_pressed = options.alt_pressed,
      .preserve_selection_on_miss = options.preserve_selection_on_miss,
      .preserve_selection_on_selected_hit =
          options.preserve_selection_on_selected_hit,
  };
}

ModelingHoverRequest hover_at(QVec3 origin, HoverOptions options) {
  return {
      .ray = forward_ray(origin),
      .selected_modifier_pressed = options.selected_modifier_pressed,
  };
}

ModelingToolRayEvent edge_loop_event(QVec3 origin) {
  return {
      .screen_position = {},
      .ray = forward_ray(origin),
      .camera = orthographic_camera(),
      .grid_size = 1.0,
      .vertex_radius = 0.3,
      .edge_radius = 0.2,
      .snap_enabled = false,
  };
}

ModelingToolRayEvent drawn_plane_event(float x, bool dragging) {
  ModelingViewportCamera camera = orthographic_camera();
  camera.orthographic = false;
  return {
      .screen_position = {},
      .ray =
          {
              .origin = {x, 2.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f},
          },
      .camera = camera,
      .grid_size = 0.5,
      .snap_enabled = true,
      .dragging = dragging,
  };
}

ModelingTransformDragBeginRequest
transform_drag_begin(DragBeginOptions options) {
  return {
      .tool = options.tool,
      .operation = options.operation,
      .axis = options.axis,
      .pivot = options.pivot,
      .screen_position = options.screen_position,
      .screen_pivot = options.screen_pivot,
      .screen_tip = options.screen_tip,
      .has_world_direction = options.has_world_direction,
      .world_direction = options.world_direction,
      .ray = forward_ray(),
      .camera = orthographic_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.3,
      .grid_snap_enabled = options.grid_snap_enabled,
      .duplicate_selected = options.duplicate_selected,
  };
}

ModelingTransformDragUpdateRequest
transform_drag_update(QVec2 screen_position) {
  return {
      .screen_position = screen_position,
      .ray = forward_ray(),
      .camera = orthographic_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.3,
      .grid_snap_enabled = true,
      .precision = false,
  };
}

ModelingTransformDragUpdateRequest transform_drag_update(float screen_x) {
  return transform_drag_update(QVec2{screen_x, 0.0f});
}

ModelingTransformDragBeginRequest x_move_drag_begin(DragBeginOptions options) {
  return transform_drag_begin(options);
}

ModelingTransformDragUpdateRequest x_move_drag_update(float screen_x) {
  return transform_drag_update(screen_x);
}

} // namespace quader::test

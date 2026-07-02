////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "editor/modeling/modeling_types.hpp"

namespace quader::test {

/**
 * Represents a Camera Spec value used by the behavior test DSL.
 */
struct CameraSpec {
    QVec3 origin = {0.0f, 0.0f, 5.0f};
    QVec2 viewport_size = {1280.0f, 720.0f};
    double orthographic_size = 12.0;
};

/**
 * Stores the Pick Options data contract used by the behavior test DSL.
 */
struct PickOptions {
  int click_count = 1;
  bool control_pressed = false;
  bool shift_pressed = false;
  bool command_pressed = false;
  bool alt_pressed = false;
  bool preserve_selection_on_miss = false;
  bool preserve_selection_on_selected_hit = false;
};

/**
 * Stores the Hover Options data contract used by the behavior test DSL.
 */
struct HoverOptions {
  bool selected_modifier_pressed = false;
};

/**
 * Stores the Drag Begin Options data contract used by the behavior test DSL.
 */
struct DragBeginOptions {
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformAxis axis = ModelingTransformAxis::X;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    QVec3 pivot = {};
    bool use_selection_center = true;
    QVec2 screen_position = {0.0f, 0.0f};
    QVec2 screen_pivot = {0.0f, 0.0f};
    QVec2 screen_tip = {10.0f, 0.0f};
    bool has_world_direction = true;
    QVec3 world_direction = {1.0f, 0.0f, 0.0f};
    bool grid_snap_enabled = true;
    bool duplicate_selected = false;
};

[[nodiscard]] ModelingViewportCamera orthographic_camera(CameraSpec spec = {});
[[nodiscard]] ModelingRay forward_ray(QVec3 origin = {0.0f, 0.0f, 5.0f});
[[nodiscard]] ModelingPickSelectRequest pick_at(QVec3 origin,
                                                PickOptions options = {});
[[nodiscard]] ModelingHoverRequest hover_at(QVec3 origin,
                                            HoverOptions options = {});
[[nodiscard]] ModelingToolRayEvent edge_loop_event(QVec3 origin = {1.0f, 0.0f,
                                                                   5.0f});
[[nodiscard]] ModelingToolRayEvent drawn_plane_event(float x,
                                                     bool dragging = false);
[[nodiscard]] ModelingTransformDragBeginRequest
transform_drag_begin(DragBeginOptions options = {});
[[nodiscard]] ModelingTransformDragUpdateRequest
transform_drag_update(QVec2 screen_position);
[[nodiscard]] ModelingTransformDragUpdateRequest
transform_drag_update(float screen_x);
[[nodiscard]] ModelingTransformDragBeginRequest
x_move_drag_begin(DragBeginOptions options = {});
[[nodiscard]] ModelingTransformDragUpdateRequest
x_move_drag_update(float screen_x);

} // namespace quader::test

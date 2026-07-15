////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_context_menu_policy.hpp"

namespace quader {

bool is_viewport_context_menu_click_gesture(
    const ViewportContextMenuOpenRequest &request) {
  if (request.modal_tool_owns_viewport || !request.right_press_tracked ||
      !request.right_released || !request.release_inside_viewport) {
    return false;
  }

  const QVec2 delta = request.release_screen - request.press_screen;
  const float release_distance_squared = delta.x * delta.x + delta.y * delta.y;
  const float drag_distance_squared =
      request.accumulated_drag_delta.x * request.accumulated_drag_delta.x +
      request.accumulated_drag_delta.y * request.accumulated_drag_delta.y;
  return release_distance_squared <= request.max_click_distance_squared &&
         drag_distance_squared <= request.max_click_distance_squared;
}

bool viewport_context_menu_has_available_actions(
    bool has_modeling_actions, bool can_create_instance,
    bool can_flatten_instance) {
  return has_modeling_actions || can_create_instance || can_flatten_instance;
}

bool should_open_viewport_context_menu(
    const ViewportContextMenuOpenRequest &request) {
  return is_viewport_context_menu_click_gesture(request) &&
         request.target_hit && request.has_actions;
}

bool should_preserve_viewport_context_menu_hover(
    const ViewportContextMenuHoverRequest &request) {
  if (!request.right_press_tracked || request.fly_hud_active) {
    return false;
  }

  const float drag_distance_squared =
      request.accumulated_drag_delta.x * request.accumulated_drag_delta.x +
      request.accumulated_drag_delta.y * request.accumulated_drag_delta.y;
  return drag_distance_squared <= request.max_click_distance_squared;
}

} // namespace quader

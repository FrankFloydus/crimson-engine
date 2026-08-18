////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_input_router.hpp"

#include "diagnostics/profile.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

ViewportInputActionKind
pressed_navigation_action(const ViewportInputFrame &input) {
  if (input.navigation_bindings.fly_pressed) {
    return ViewportInputActionKind::Fly;
  }
  if (input.navigation_bindings.pan_pressed) {
    return ViewportInputActionKind::Pan;
  }
  if (input.navigation_bindings.orbit_snap_pressed) {
    return ViewportInputActionKind::OrbitSnap;
  }
  if (input.navigation_bindings.orbit_pressed) {
    return ViewportInputActionKind::Orbit;
  }
  return ViewportInputActionKind::None;
}

bool navigation_action_down(const ViewportInputFrame &input,
                            ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    return input.navigation_bindings.orbit_down;
  case ViewportInputActionKind::OrbitSnap:
    return input.navigation_bindings.orbit_snap_down;
  case ViewportInputActionKind::Pan:
    return input.navigation_bindings.pan_down;
  case ViewportInputActionKind::Fly:
    return input.navigation_bindings.fly_down;
  default:
    return false;
  }
}

bool navigation_action_released(const ViewportInputFrame &input,
                                ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    return input.navigation_bindings.orbit_released;
  case ViewportInputActionKind::OrbitSnap:
    return input.navigation_bindings.orbit_snap_released;
  case ViewportInputActionKind::Pan:
    return input.navigation_bindings.pan_released;
  case ViewportInputActionKind::Fly:
    return input.navigation_bindings.fly_released;
  default:
    return false;
  }
}

bool is_orbit_navigation_action(ViewportInputActionKind action) {
  return action == ViewportInputActionKind::Orbit ||
         action == ViewportInputActionKind::OrbitSnap;
}

ViewportDragMode
navigation_mode_for_action(ViewportInputActionKind action,
                           const ViewportCameraController &camera) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    return ViewportDragMode::Orbit;
  case ViewportInputActionKind::OrbitSnap:
    return ViewportDragMode::OrbitSnap;
  case ViewportInputActionKind::Pan:
    return ViewportDragMode::Pan;
  case ViewportInputActionKind::Fly:
    return camera.state().projection == ViewportProjection::Orthographic
               ? ViewportDragMode::Pan
               : ViewportDragMode::Fly;
  default:
    return ViewportDragMode::None;
  }
}

} // namespace

ViewportInputRouter::ViewportInputRouter() {
  sync_grid_preset_snapshot(viewport_default_grid_preset());
}

ViewportInputResult
ViewportInputRouter::handle_frame(const ViewportInputFrame &input,
                                  ViewportCameraController &camera) {
  QDR_PROFILE_FUNCTION();
  ViewportInputResult result;
  camera.set_viewport_size(input.viewport_size.x, input.viewport_size.y);

  const bool accepts_mouse = input.viewport_hovered || navigating();
  const bool has_fly_movement_input =
      std::abs(input.fly_local_direction.x) > 0.000001f ||
      std::abs(input.fly_local_direction.y) > 0.000001f ||
      std::abs(input.fly_local_direction.z) > 0.000001f;
  bool fly_started_this_frame = false;
  const ViewportInputActionKind pressed_action =
      accepts_mouse && !navigating() ? pressed_navigation_action(input)
                                     : ViewportInputActionKind::None;
  if (pressed_action != ViewportInputActionKind::None) {
    QDR_PROFILE_SCOPE("Viewport navigation begin");
    active_navigation_action_ = pressed_action;
    navigation_mode_ = navigation_mode_for_action(pressed_action, camera);
    orbit_snap_action_uses_alt_modifier_ =
        pressed_action == ViewportInputActionKind::OrbitSnap &&
        input.modifiers.alt;
    fly_cursor_capture_active_ = navigation_mode_ == ViewportDragMode::Fly;
    fly_hud_active_ = false;
    camera.begin_drag(input.mouse_screen, navigation_mode_);
    fly_started_this_frame = navigation_mode_ == ViewportDragMode::Fly;
    result.consumed = true;
  }

  if (navigation_mode_ == ViewportDragMode::Fly) {
    QDR_PROFILE_SCOPE("Viewport fly navigation");
    if (input.navigation_bindings.reset_fly_speed_pressed) {
      camera.reset_fly_speed();
      result.consumed = true;
      result.fly_speed_changed = true;
      result.fly_speed_reset = true;
      result.fly_speed = camera.fly_speed();
    }

    const bool fly_speed_wheel_active =
        input.navigation_bindings.zoom_wheel ||
        input.navigation_bindings.zoom_to_cursor_wheel;
    if (input.mouse_wheel != 0.0f && fly_speed_wheel_active) {
      camera.adjust_fly_speed(input.mouse_wheel);
      result.consumed = true;
      result.fly_speed_changed = true;
      result.fly_speed = camera.fly_speed();
    }

    const bool cancel_fly = input.navigation_bindings.cancel_navigation_pressed;
    const bool fly_down =
        navigation_action_down(input, active_navigation_action_);
    if (fly_down && !cancel_fly && !fly_started_this_frame) {
      if (has_fly_movement_input) {
        fly_hud_active_ = true;
      }
      camera.update_drag_delta(input.mouse_delta);
      camera.fly_move(input.fly_local_direction, input.delta_seconds);
      result.consumed = true;
      result.camera_changed = true;
    }

    if (navigation_action_released(input, active_navigation_action_) ||
        !fly_down || cancel_fly) {
      navigation_mode_ = ViewportDragMode::None;
      active_navigation_action_ = ViewportInputActionKind::None;
      orbit_snap_action_uses_alt_modifier_ = false;
      fly_cursor_capture_active_ = false;
      fly_hud_active_ = false;
      camera.end_drag();
      result.consumed = true;
    }

    result.navigation_active = navigating();
    result.fly_navigation_active = fly_navigating();
    result.fly_cursor_capture_active = fly_cursor_capture_active();
    result.fly_hud_active = fly_hud_active();
    return result;
  }

  if (navigating()) {
    QDR_PROFILE_SCOPE("Viewport drag navigation");
    const bool drag_down =
        navigation_action_down(input, active_navigation_action_);
    const bool drag_released =
        navigation_action_released(input, active_navigation_action_) ||
        input.navigation_bindings.cancel_navigation_pressed;
    if (drag_down) {
      const ViewportDragMode active_navigation_mode =
          is_orbit_navigation_action(active_navigation_action_)
              ? (input.modifiers.alt ||
                         (active_navigation_action_ ==
                              ViewportInputActionKind::OrbitSnap &&
                          !orbit_snap_action_uses_alt_modifier_)
                     ? ViewportDragMode::OrbitSnap
                     : ViewportDragMode::Orbit)
              : navigation_mode_;
      const bool locked_orthographic_orbit =
          camera.orthographic_view_locked() &&
          (active_navigation_mode == ViewportDragMode::Orbit ||
           active_navigation_mode == ViewportDragMode::OrbitSnap);
      if (!locked_orthographic_orbit) {
        if (active_navigation_mode == ViewportDragMode::Orbit ||
            active_navigation_mode == ViewportDragMode::OrbitSnap) {
          QDR_PROFILE_SCOPE("Viewport navigation orbit");
          camera.update_drag(input.mouse_screen, active_navigation_mode);
        } else {
          QDR_PROFILE_SCOPE("Viewport navigation pan");
          camera.update_drag(input.mouse_screen, active_navigation_mode);
        }
        result.camera_changed = true;
      }
      navigation_mode_ = active_navigation_mode;
      result.consumed = true;
    }
    if (drag_released || !drag_down) {
      navigation_mode_ = ViewportDragMode::None;
      active_navigation_action_ = ViewportInputActionKind::None;
      orbit_snap_action_uses_alt_modifier_ = false;
      fly_cursor_capture_active_ = false;
      fly_hud_active_ = false;
      camera.end_drag();
      result.consumed = true;
    }
  }

  const bool wheel_zoom_active = input.navigation_bindings.zoom_wheel ||
                                 input.navigation_bindings.zoom_to_cursor_wheel;
  if (input.viewport_hovered && input.mouse_wheel != 0.0f &&
      wheel_zoom_active) {
    QDR_PROFILE_SCOPE("Viewport wheel zoom");
    if (input.navigation_bindings.zoom_to_cursor_wheel) {
      camera.scroll_to_cursor(input.mouse_wheel, input.mouse_local);
      result.zoomed_to_cursor = true;
    } else {
      camera.scroll(input.mouse_wheel);
    }
    result.consumed = true;
    result.camera_changed = true;
  }

  const bool accepts_shortcuts =
      input.viewport_focused && !input.text_input_active && !navigating();
  if (accepts_shortcuts) {
    QDR_PROFILE_SCOPE("Viewport shortcuts");
    if (input.keybinding_input_consumed) {
      result.consumed = true;
    }
    for (const ViewportShortcut &shortcut : input.resolved_shortcuts) {
      if (shortcut.kind == ViewportShortcutKind::None) {
        continue;
      }
      if (handle_shortcut(shortcut, camera, result,
                          input.active_tool_blocks_focus_shortcuts)) {
        result.consumed = true;
        result.shortcut_handled = true;
        result.shortcut = shortcut;
      }
    }
  }

  result.navigation_active = navigating();
  result.fly_navigation_active = fly_navigating();
  result.fly_cursor_capture_active = fly_cursor_capture_active();
  result.fly_hud_active = fly_hud_active();
  return result;
}

void ViewportInputRouter::reset() {
  navigation_mode_ = ViewportDragMode::None;
  active_navigation_action_ = ViewportInputActionKind::None;
  orbit_snap_action_uses_alt_modifier_ = false;
  fly_cursor_capture_active_ = false;
  fly_hud_active_ = false;
}

void ViewportInputRouter::set_active_tool(ViewportToolId tool) {
  state_.active_tool = tool;
}

void ViewportInputRouter::set_selection_mode(ViewportSelectionMode mode) {
  state_.selection_mode = mode;
}

void ViewportInputRouter::sync_grid_preset_snapshot(int preset) {
  state_.grid_preset = viewport_clamp_grid_preset(preset);
}

bool ViewportInputRouter::handle_shortcut(const ViewportShortcut &shortcut,
                                          ViewportCameraController &camera,
                                          ViewportInputResult &result,
                                          bool active_tool_blocks_focus_shortcuts) {
  switch (shortcut.kind) {
  case ViewportShortcutKind::ActivateTool:
    result.tool_changed = result.tool_changed ||
                          state_.active_tool != shortcut.tool;
    state_.active_tool = shortcut.tool;
    result.tool_activation_requested = true;
    return true;
  case ViewportShortcutKind::SetSelectionMode:
    result.selection_mode_changed =
        result.selection_mode_changed ||
        state_.selection_mode != shortcut.selection_mode;
    state_.selection_mode = shortcut.selection_mode;
    return true;
  case ViewportShortcutKind::FocusDocument:
    if (active_tool_blocks_focus_shortcuts) {
      return false;
    }
    result.focus_document_requested = true;
    result.camera_changed = true;
    return true;
  case ViewportShortcutKind::FocusOrigin:
    camera.focus_origin();
    result.camera_changed = true;
    return true;
  case ViewportShortcutKind::ToggleMaximized:
    if (active_tool_blocks_focus_shortcuts) {
      return false;
    }
    result.maximized_toggle_requested = true;
    return true;
  case ViewportShortcutKind::SetGridPreset:
    result.requested_grid_preset =
        viewport_clamp_grid_preset(shortcut.grid_preset);
    result.grid_preset_changed =
        state_.grid_preset != result.requested_grid_preset;
    return true;
  case ViewportShortcutKind::SetView:
    result.requested_view = shortcut.view;
    result.view_changed = true;
    return true;
  case ViewportShortcutKind::CancelActiveTool:
    result.cancel_requested = true;
    return true;
  case ViewportShortcutKind::DeletePopup:
    return !active_tool_blocks_focus_shortcuts;
  case ViewportShortcutKind::FlipMeshNormals:
    return !active_tool_blocks_focus_shortcuts &&
           state_.selection_mode == ViewportSelectionMode::Object;
  case ViewportShortcutKind::FlipHorizontal:
  case ViewportShortcutKind::FlipVertical:
    return state_.selection_mode == ViewportSelectionMode::Object;
  case ViewportShortcutKind::DeleteSelection:
  case ViewportShortcutKind::SelectAll:
  case ViewportShortcutKind::InvertSelection:
  case ViewportShortcutKind::Copy:
  case ViewportShortcutKind::Paste:
  case ViewportShortcutKind::Duplicate:
  case ViewportShortcutKind::Undo:
  case ViewportShortcutKind::Redo:
  case ViewportShortcutKind::RepeatLastAction:
  case ViewportShortcutKind::MergeActive:
  case ViewportShortcutKind::MergeCenter:
  case ViewportShortcutKind::MirrorAxis:
  case ViewportShortcutKind::ApplyMaterial:
    return true;
  case ViewportShortcutKind::None:
    return false;
  }
  return false;
}

const char *viewport_tool_id(ViewportToolId tool) {
  switch (tool) {
  case ViewportToolId::Select:
    return "select";
  case ViewportToolId::Move:
    return "move";
  case ViewportToolId::Extrude:
    return "extrude";
  case ViewportToolId::Rotate:
    return "rotate";
  case ViewportToolId::Scale:
    return "scale";
  case ViewportToolId::CreatePrimitive:
    return "create_primitive";
  case ViewportToolId::InsertEdgeLoop:
    return "insert_edge_loop";
  case ViewportToolId::Knife:
    return "knife";
  case ViewportToolId::Cut:
    return "cut";
  case ViewportToolId::Poly:
    return "poly";
  case ViewportToolId::Mirror:
    return "mirror";
  case ViewportToolId::Pivot:
    return "pivot";
  case ViewportToolId::LinearArray:
    return "linear_array";
  case ViewportToolId::RadialArray:
    return "radial_array";
  }
  return "unknown";
}

const char *viewport_selection_mode_id(ViewportSelectionMode mode) {
  switch (mode) {
  case ViewportSelectionMode::Vertex:
    return "vertex";
  case ViewportSelectionMode::Edge:
    return "edge";
  case ViewportSelectionMode::Face:
    return "face";
  case ViewportSelectionMode::Object:
    return "object";
  case ViewportSelectionMode::Group:
    return "group";
  }
  return "unknown";
}

float viewport_grid_world_size_for_preset(int preset) {
  return viewport_grid_world_size_from_contract(preset);
}

} // namespace quader

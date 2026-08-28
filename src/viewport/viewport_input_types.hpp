////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_input_types.hpp"
#include "api/quader_input_types.hpp"
#include "math/types.hpp"

#include <vector>

namespace quader {

/**
 * Stores per-frame semantic viewport navigation input resolved from raw bindings.
 */
struct ViewportNavigationInput {
  bool orbit_pressed = false;
  bool orbit_down = false;
  bool orbit_released = false;
  bool orbit_snap_pressed = false;
  bool orbit_snap_down = false;
  bool orbit_snap_released = false;
  bool pan_pressed = false;
  bool pan_down = false;
  bool pan_released = false;
  bool fly_pressed = false;
  bool fly_down = false;
  bool fly_released = false;
  bool zoom_wheel = false;
  bool zoom_to_cursor_wheel = false;
  bool reset_fly_speed_pressed = false;
  bool cancel_navigation_pressed = false;
  bool fly_forward_down = false;
  bool fly_backward_down = false;
  bool fly_right_down = false;
  bool fly_left_down = false;
};

/**
 * Stores the Viewport Input Frame data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportInputFrame {
  bool viewport_hovered = false;
  bool viewport_focused = false;
  bool orthographic_view = false;
  bool text_input_active = false;
  bool navigation_active = false;
  bool fly_navigation_active = false;
  bool orbit_navigation_active = false;
  bool pan_navigation_active = false;
  bool modal_tool_active = false;
  bool active_tool_owns_viewport = false;
  bool active_tool_blocks_focus_shortcuts = false;
  bool active_transform_drag = false;
  bool operation_preview_active = false;
  bool modal_tool_finalize_pressed = false;
  bool modal_tool_mode_cycle_pressed = false;
  bool quick_pivot_pressed = false;
  bool quick_pivot_down = false;
  bool quick_pivot_released = false;
  bool tool_constraint_x_pressed = false;
  bool tool_constraint_y_pressed = false;
  bool tool_constraint_z_pressed = false;
  int linear_array_copy_delta = 0;
  int radial_array_copy_delta = 0;
  bool sample_face_material_pressed = false;
  bool apply_active_material_to_selection_pressed = false;
  bool apply_active_material_delay_for_double_click = false;
  bool apply_face_material_to_mesh_pressed = false;
  bool transfer_face_texture_pressed = false;
  // Canonical left Click/DoubleClick counterparts and Qt's substituted second
  // press reserve pointer ownership before selection can replace the source.
  bool transfer_face_texture_pointer_reserved = false;
  // An unconsumed first left click can wait for any bound depth double-click.
  bool selection_click_delay_for_double_click = false;
  // Physical first-click modifiers, before they can change for the double-click.
  ViewportModifiers selection_click_modifiers = {};
  bool cycle_selection_depth_pressed = false;
  // Carries the resolved selection operation independently of live modifiers.
  ViewportModifiers cycle_selection_depth_modifiers = {};
  bool middle_pressed = false;
  bool middle_down = false;
  bool middle_released = false;
  bool right_pressed = false;
  bool right_down = false;
  bool right_released = false;
  float mouse_wheel = 0.0f;
  float delta_seconds = 1.0f / 60.0f;
  QVec2 mouse_screen = {};
  QVec2 mouse_delta = {};
  QVec2 mouse_local = {};
  QVec2 viewport_size = {};
  int grid_preset = 6;
  ViewportModifiers modifiers = {};
  QVec3 fly_local_direction = {};
  std::vector<QuaderInputChord> pressed_input_chords;
  std::vector<QuaderInputChord> active_input_chords;
  std::vector<QuaderInputChord> released_input_chords;
  std::vector<QuaderWheelInputEdge> wheel_input_edges;
  bool keybinding_input_consumed = false;
  std::vector<ViewportShortcut> resolved_shortcuts;
  ViewportNavigationInput navigation_bindings = {};
};

/**
 * Stores the Viewport Input Result data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportInputResult {
    bool consumed = false;
    bool camera_changed = false;
    bool shortcut_handled = false;
    bool tool_activation_requested = false;
    bool tool_changed = false;
    bool selection_mode_changed = false;
    bool grid_preset_changed = false;
    bool view_changed = false;
    bool navigation_active = false;
    bool fly_navigation_active = false;
    bool fly_cursor_capture_active = false;
    bool fly_hud_active = false;
    bool fly_speed_changed = false;
    bool fly_speed_reset = false;
    float fly_speed = 0.0f;
    bool zoomed_to_cursor = false;
    bool cancel_requested = false;
    bool focus_document_requested = false;
    bool maximized_toggle_requested = false;
    int requested_grid_preset = 6;
    ViewportView requested_view = ViewportView::Perspective;
    ViewportShortcut shortcut = {};
};

const char *viewport_tool_id(ViewportToolId tool);
const char *viewport_selection_mode_id(ViewportSelectionMode mode);
float viewport_grid_world_size_for_preset(int preset);

} // namespace quader

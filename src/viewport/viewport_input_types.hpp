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

#include <cstdint>
#include <optional>
#include <vector>

namespace quader {

/** Selects toolkit-neutral pointer feedback requested by an active tool. */
enum class ViewportPointerCursor {
  Default,
  PrecisionCross,
};

/// Carries one canonical mouse-button edge with its event-time pane position.
struct ViewportPointerButtonEdge {
  QuaderMouseButton button = QuaderMouseButton::None;
  QVec2 local = {};
  // Monotonic at the production Qt boundary. Zero is reserved for synthetic
  // callers that do not need to distinguish coalesced event ordering.
  std::uint64_t sequence = 0u;
};

/// Distinguishes ordered semantic pointer presses from releases.
enum class ViewportNavigationPointerTransitionKind {
  Pressed,
  Released,
};

/// Preserves semantic navigation ordering when several button cycles coalesce.
struct ViewportNavigationPointerTransition {
  ViewportNavigationPointerTransitionKind kind =
      ViewportNavigationPointerTransitionKind::Pressed;
  ViewportInputActionKind action = ViewportInputActionKind::None;
  ViewportPointerButtonEdge pointer_edge = {};
};

/// Preserves one resolved physical wheel event through camera dispatch.
struct ViewportNavigationWheelEvent {
  ViewportInputActionKind action = ViewportInputActionKind::None;
  float steps = 0.0f;
  QVec2 local = {};
};

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
  // Each navigation action retains the physical edge that produced it. More
  // than one semantic action can resolve in a coalesced frame, so the shared
  // navigation-press selector chooses the winner without losing provenance;
  // release provenance bounds the final delta before unrelated motion.
  std::optional<ViewportPointerButtonEdge> orbit_pressed_pointer_edge;
  std::optional<ViewportPointerButtonEdge> orbit_snap_pressed_pointer_edge;
  std::optional<ViewportPointerButtonEdge> pan_pressed_pointer_edge;
  std::optional<ViewportPointerButtonEdge> fly_pressed_pointer_edge;
  std::optional<ViewportPointerButtonEdge> orbit_released_pointer_edge;
  std::optional<ViewportPointerButtonEdge> orbit_snap_released_pointer_edge;
  std::optional<ViewportPointerButtonEdge> pan_released_pointer_edge;
  std::optional<ViewportPointerButtonEdge> fly_released_pointer_edge;
  // Production entries are sorted by nonzero pointer sequence. Aggregate
  // fields above remain the lightweight fallback for synthetic callers.
  std::vector<ViewportNavigationPointerTransition> pointer_transitions;
  // Production wheel input remains ordered so opposing packets cannot cancel
  // before each packet observes the camera's current zoom boundary.
  std::vector<ViewportNavigationWheelEvent> wheel_events;
};

/// Describes one ordered semantic action that owns a physical pointer press.
enum class ViewportSemanticPointerEdgeKind {
  ReservePress,
  Trigger,
};

/**
 * Couples semantic intent to the exact raw pointer edge that produced it.
 * Keyboard bindings intentionally carry no pointer provenance.
 */
struct ViewportSemanticPointerEdge {
  ViewportSemanticPointerEdgeKind kind =
      ViewportSemanticPointerEdgeKind::Trigger;
  QuaderInputChord semantic_chord = {};
  std::optional<ViewportPointerButtonEdge> pointer_edge;
};

using ViewportCurveConversionEdgeKind = ViewportSemanticPointerEdgeKind;
using ViewportCurveConversionEdge = ViewportSemanticPointerEdge;

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
  // One selected Curve in Object/Mesh mode exposes the same Curve-control
  // semantic actions without activating the Curve creation tool.
  bool curve_controls_editable = false;
  bool modal_tool_finalize_pressed = false;
  bool modal_tool_mode_cycle_pressed = false;
  bool quick_pivot_pressed = false;
  bool quick_pivot_down = false;
  bool quick_pivot_released = false;
  bool transform_vertex_snap_down = false;
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
  // Canonical mouse Click/DoubleClick counterparts and Qt's substituted second
  // press reserve pointer ownership before selection can replace the source.
  bool transfer_face_texture_pointer_reserved = false;
  // Retains the exact Settings-resolved button, gesture, and physical edge.
  // The interaction owner uses this instead of assuming a left-button action.
  std::vector<ViewportSemanticPointerEdge> transfer_face_texture_edges;
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
  // Qt reports local motion in physical pixels so pan remains proportional to
  // the physical render surface. Orbit is defined in logical pixels and uses
  // this scale to remain stable across Windows display scaling.
  float device_pixel_ratio = 1.0f;
  // A gesture-local world-space depth/pivot hint. The UI samples it only when
  // navigation starts; the camera owns the resulting navigation state.
  std::optional<QVec3> navigation_anchor;
  // Whole-scene depth is only a legacy-state repair fallback. It may pull a
  // severely poisoned target inward, but must never replace a valid nearer
  // orbit/pan target with distant scene bounds.
  bool navigation_anchor_repair_only = false;
  int grid_preset = 6;
  ViewportModifiers modifiers = {};
  QVec3 fly_local_direction = {};
  std::vector<QuaderInputChord> pressed_input_chords;
  std::vector<QuaderInputChord> active_input_chords;
  std::vector<QuaderInputChord> released_input_chords;
  std::vector<ViewportPointerButtonEdge> pointer_pressed_edges;
  std::vector<ViewportPointerButtonEdge> pointer_released_edges;
  std::vector<ViewportPointerButtonEdge> pointer_drag_started_edges;
  std::vector<QuaderWheelInputEdge> wheel_input_edges;
  bool keybinding_input_consumed = false;
  std::vector<ViewportShortcut> resolved_shortcuts;
  ViewportNavigationInput navigation_bindings = {};
  // A real focus/capture loss remains a cancellation even when the host keeps
  // routing the captured pane as logically hovered and focused.
  bool pointer_interaction_canceled = false;
  // Retains Curve conversion reserves and triggers in physical dispatch order.
  std::vector<ViewportCurveConversionEdge> curve_conversion_edges;
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

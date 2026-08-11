////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/input/viewport_keybinding_resolver.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/viewport_input_types.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace quader {
namespace {

void mark_navigation_pressed(ViewportNavigationInput &navigation,
                             ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_pressed = true;
    navigation.orbit_down = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_pressed = true;
    navigation.orbit_snap_down = true;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_pressed = true;
    navigation.pan_down = true;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_pressed = true;
    navigation.fly_down = true;
    break;
  case ViewportInputActionKind::ResetFlySpeed:
    navigation.reset_fly_speed_pressed = true;
    break;
  case ViewportInputActionKind::CancelNavigation:
    navigation.cancel_navigation_pressed = true;
    break;
  case ViewportInputActionKind::FlyForward:
    navigation.fly_forward_down = true;
    break;
  case ViewportInputActionKind::FlyBackward:
    navigation.fly_backward_down = true;
    break;
  case ViewportInputActionKind::FlyRight:
    navigation.fly_right_down = true;
    break;
  case ViewportInputActionKind::FlyLeft:
    navigation.fly_left_down = true;
    break;
  default:
    break;
  }
}

void mark_action_pressed(ViewportInputFrame &input,
                         ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::ModalToolFinalize:
    input.modal_tool_finalize_pressed = true;
    break;
  case ViewportInputActionKind::ModalToolModeCycle:
    input.modal_tool_mode_cycle_pressed = true;
    break;
  case ViewportInputActionKind::QuickPivot:
    input.quick_pivot_pressed = true;
    input.quick_pivot_down = true;
    break;
  case ViewportInputActionKind::ToolConstraintX:
    input.tool_constraint_x_pressed = true;
    break;
  case ViewportInputActionKind::ToolConstraintY:
    input.tool_constraint_y_pressed = true;
    break;
  case ViewportInputActionKind::ToolConstraintZ:
    input.tool_constraint_z_pressed = true;
    break;
  case ViewportInputActionKind::LinearArrayIncreaseCopies:
    ++input.linear_array_copy_delta;
    break;
  case ViewportInputActionKind::LinearArrayDecreaseCopies:
    --input.linear_array_copy_delta;
    break;
  case ViewportInputActionKind::RadialArrayIncreaseCopies:
    ++input.radial_array_copy_delta;
    break;
  case ViewportInputActionKind::RadialArrayDecreaseCopies:
    --input.radial_array_copy_delta;
    break;
  case ViewportInputActionKind::SampleFaceMaterial:
    input.sample_face_material_pressed = true;
    break;
  case ViewportInputActionKind::ApplyActiveMaterialToSelection:
    input.apply_active_material_to_selection_pressed = true;
    break;
  case ViewportInputActionKind::ApplyFaceMaterialToMesh:
    input.apply_face_material_to_mesh_pressed = true;
    break;
  default:
    mark_navigation_pressed(input.navigation_bindings, action);
    break;
  }
}

void mark_navigation_down(ViewportNavigationInput &navigation,
                          ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_down = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_down = true;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_down = true;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_down = true;
    break;
  case ViewportInputActionKind::Zoom:
    navigation.zoom_wheel = true;
    break;
  case ViewportInputActionKind::ZoomToCursor:
    navigation.zoom_to_cursor_wheel = true;
    break;
  case ViewportInputActionKind::FlyForward:
    navigation.fly_forward_down = true;
    break;
  case ViewportInputActionKind::FlyBackward:
    navigation.fly_backward_down = true;
    break;
  case ViewportInputActionKind::FlyRight:
    navigation.fly_right_down = true;
    break;
  case ViewportInputActionKind::FlyLeft:
    navigation.fly_left_down = true;
    break;
  default:
    break;
  }
}

void mark_action_down(ViewportInputFrame &input,
                      ViewportInputActionKind action) {
  if (action == ViewportInputActionKind::QuickPivot) {
    input.quick_pivot_down = true;
  } else {
    mark_navigation_down(input.navigation_bindings, action);
  }
}

void mark_navigation_released(ViewportNavigationInput &navigation,
                              ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_released = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_released = true;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_released = true;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_released = true;
    break;
  default:
    break;
  }
}

void mark_action_released(ViewportInputFrame &input,
                          ViewportInputActionKind action) {
  if (action == ViewportInputActionKind::QuickPivot) {
    input.quick_pivot_released = true;
  } else {
    mark_navigation_released(input.navigation_bindings, action);
  }
}

void sync_fly_direction(ViewportInputFrame &input) {
  input.fly_local_direction = {};
  if (input.navigation_bindings.fly_forward_down) {
    input.fly_local_direction.z += 1.0f;
  }
  if (input.navigation_bindings.fly_backward_down) {
    input.fly_local_direction.z -= 1.0f;
  }
  if (input.navigation_bindings.fly_right_down) {
    input.fly_local_direction.x += 1.0f;
  }
  if (input.navigation_bindings.fly_left_down) {
    input.fly_local_direction.x -= 1.0f;
  }
}

QuaderViewportKeybindingContext
keybinding_context(const ViewportInputFrame &input) {
  return {
      .viewport_hovered = input.viewport_hovered,
      .navigation_active = input.navigation_active,
      .fly_navigation_active = input.fly_navigation_active,
      .orbit_navigation_active = input.orbit_navigation_active,
      .pan_navigation_active = input.pan_navigation_active,
      .modal_tool_active = input.modal_tool_active,
      .active_tool_owns_viewport = input.active_tool_owns_viewport,
      .active_transform_drag = input.active_transform_drag,
      .operation_preview_active = input.operation_preview_active,
  };
}

bool resolve_action(QuaderEditorApi &editor, const ViewportToolState &state,
                    const ViewportInputFrame &input,
                    const QuaderInputChord &chord,
                    ViewportInputActionKind &action) {
  return editor.commands().resolve_viewport_input_action(
      chord, state, input.viewport_focused, input.text_input_active, action,
      keybinding_context(input));
}

bool is_material_action(ViewportInputActionKind action) {
  return action == ViewportInputActionKind::SampleFaceMaterial ||
         action ==
             ViewportInputActionKind::ApplyActiveMaterialToSelection ||
         action == ViewportInputActionKind::ApplyFaceMaterialToMesh;
}

bool is_mouse_click(const QuaderInputChord &chord) {
  return chord.device == QuaderInputDevice::Mouse &&
         chord.mouse_button != QuaderMouseButton::None &&
         chord.gesture == QuaderInputGesture::Click;
}

std::optional<ViewportInputActionKind> paired_mouse_double_click_action(
    QuaderEditorApi &editor, const ViewportToolState &state,
    const ViewportInputFrame &input, const QuaderInputChord &chord) {
  if (!is_mouse_click(chord)) {
    return std::nullopt;
  }

  QuaderInputChord paired_double_click = chord;
  paired_double_click.gesture = QuaderInputGesture::DoubleClick;
  ViewportInputActionKind paired_action = ViewportInputActionKind::None;
  if (!resolve_action(editor, state, input, paired_double_click,
                      paired_action)) {
    return std::nullopt;
  }
  return paired_action;
}

bool apply_active_material_needs_double_click_delay(
    QuaderEditorApi &editor, const ViewportToolState &state,
    const ViewportInputFrame &input, const QuaderInputChord &chord) {
  if (!is_mouse_click(chord)) {
    return false;
  }
  const std::optional<ViewportInputActionKind> paired_action =
      paired_mouse_double_click_action(editor, state, input, chord);
  return paired_action == ViewportInputActionKind::ApplyFaceMaterialToMesh;
}

void record_material_action_provenance(
    QuaderEditorApi &editor, const ViewportToolState &state,
    ViewportInputFrame &input, const QuaderInputChord &chord,
    ViewportInputActionKind action) {
  if (!is_material_action(action)) {
    return;
  }

  if (action ==
      ViewportInputActionKind::ApplyActiveMaterialToSelection) {
    const bool requires_delay =
        apply_active_material_needs_double_click_delay(editor, state, input,
                                                       chord);
    input.apply_active_material_delay_for_double_click =
        input.apply_active_material_to_selection_pressed
            ? input.apply_active_material_delay_for_double_click &&
                  requires_delay
            : requires_delay;
  }
}

void log_command_failure(std::string_view source,
                         const QuaderApiVoidResult &result) {
  if (!result.ok) {
    LOG_ERROR("{} failed: {}", source,
              result.message.empty() ? "Command failed." : result.message);
  }
}

bool apply_keybinding_dispatch(QuaderEditorApi &editor,
                               const QuaderViewportKeybindingDispatch &dispatch,
                               ViewportInputFrame &input) {
  switch (dispatch.kind) {
  case QuaderViewportKeybindingDispatch::Kind::PendingChord:
    input.keybinding_input_consumed = true;
    return true;
  case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
    input.resolved_shortcuts.push_back(dispatch.shortcut);
    return true;
  case QuaderViewportKeybindingDispatch::Kind::Command:
    log_command_failure("Keybinding command",
                        editor.commands().execute(dispatch.command_id));
    input.keybinding_input_consumed = true;
    return true;
  case QuaderViewportKeybindingDispatch::Kind::NoMatch:
    return false;
  }
  return false;
}

bool input_chord_is_suppressed(
    const std::vector<QuaderInputChord> &suppressed,
    const QuaderInputChord &chord) {
  return std::find(suppressed.begin(), suppressed.end(), chord) !=
         suppressed.end();
}

void suppress_input_chord(std::vector<QuaderInputChord> &suppressed,
                          const QuaderInputChord &chord) {
  if (!input_chord_is_suppressed(suppressed, chord)) {
    suppressed.push_back(chord);
  }
}

bool release_suppressed_input_chord(
    std::vector<QuaderInputChord> &suppressed,
    const QuaderInputChord &chord) {
  const auto found = std::find(suppressed.begin(), suppressed.end(), chord);
  if (found == suppressed.end()) {
    return false;
  }
  suppressed.erase(found);
  return true;
}


} // namespace

void ViewportKeybindingResolver::resolve(QuaderEditorApi &editor,
                         const ViewportToolState &state,
                         ViewportInputFrame &input,
                         std::vector<QuaderInputChord> &suppressed_chords,
                         double &linear_array_wheel_remainder,
                         double &radial_array_wheel_remainder) {
  input.resolved_shortcuts.clear();
  input.navigation_bindings = {};
  input.linear_array_copy_delta = 0;
  input.radial_array_copy_delta = 0;
  input.sample_face_material_pressed = false;
  input.apply_active_material_to_selection_pressed = false;
  input.apply_active_material_delay_for_double_click = false;
  input.apply_face_material_to_mesh_pressed = false;
  input.mouse_wheel = 0.0f;
  const bool array_wheel_context_active =
      input.viewport_focused && !input.text_input_active &&
      !input.navigation_active;
  if (!array_wheel_context_active ||
      state.active_tool != ViewportToolId::LinearArray) {
    linear_array_wheel_remainder = 0.0;
  }
  if (!array_wheel_context_active ||
      state.active_tool != ViewportToolId::RadialArray) {
    radial_array_wheel_remainder = 0.0;
  }
  if (!input.viewport_focused || input.text_input_active) {
    editor.commands().reset_pending_keybinding_chord();
    suppressed_chords.clear();
    return;
  }

  bool wheel_event_resolved_linear_array_action = false;
  bool wheel_event_resolved_radial_array_action = false;
  dispatch_ordered_viewport_input_edges(
      input.pressed_input_chords, input.wheel_input_edges,
      [&](const QuaderInputChord &chord, ViewportInputEdgeKind kind,
          double wheel_step_delta) {
        const bool pending_before =
            editor.commands().has_pending_keybinding_chord();
        bool stateful_dispatch_handled = false;
        const bool handled = dispatch_viewport_input_edge(
            chord, kind,
            [&]() { return pending_before; },
            [&](const QuaderInputChord &candidate,
                ViewportInputEdgeKind edge_kind) {
              ViewportInputActionKind action = ViewportInputActionKind::None;
              if (!resolve_action(editor, state, input, candidate, action)) {
                return false;
              }
              if (edge_kind == ViewportInputEdgeKind::Wheel) {
                const bool linear_increase =
                    action ==
                    ViewportInputActionKind::LinearArrayIncreaseCopies;
                const bool linear_decrease =
                    action ==
                    ViewportInputActionKind::LinearArrayDecreaseCopies;
                const bool radial_increase =
                    action ==
                    ViewportInputActionKind::RadialArrayIncreaseCopies;
                const bool radial_decrease =
                    action ==
                    ViewportInputActionKind::RadialArrayDecreaseCopies;
                if (linear_increase || linear_decrease) {
                  wheel_event_resolved_linear_array_action = true;
                  input.keybinding_input_consumed = true;
                  const double semantic_delta =
                      std::abs(wheel_step_delta) *
                      (linear_increase ? 1.0 : -1.0);
                  input.linear_array_copy_delta +=
                      accumulate_discrete_wheel_steps(
                          semantic_delta, linear_array_wheel_remainder);
                } else if (radial_increase || radial_decrease) {
                  wheel_event_resolved_radial_array_action = true;
                  input.keybinding_input_consumed = true;
                  const double semantic_delta =
                      std::abs(wheel_step_delta) *
                      (radial_increase ? 1.0 : -1.0);
                  input.radial_array_copy_delta +=
                      accumulate_discrete_wheel_steps(
                          semantic_delta, radial_array_wheel_remainder);
                } else {
                  mark_navigation_down(input.navigation_bindings, action);
                  if (action == ViewportInputActionKind::Zoom ||
                      action == ViewportInputActionKind::ZoomToCursor) {
                    input.mouse_wheel +=
                        static_cast<float>(wheel_step_delta);
                  }
                }
              } else {
                record_material_action_provenance(editor, state, input,
                                                  candidate, action);
                mark_action_pressed(input, action);
              }
              return true;
            },
            [&](const QuaderInputChord &candidate, ViewportInputEdgeKind) {
              stateful_dispatch_handled = apply_keybinding_dispatch(
                  editor,
                  editor.commands().dispatch_viewport_input_binding(
                      candidate, state, input.viewport_focused,
                      input.text_input_active, keybinding_context(input)),
                  input);
              return stateful_dispatch_handled;
            });
        if (pending_before) {
          input.keybinding_input_consumed = true;
        }
        if (kind == ViewportInputEdgeKind::Pressed &&
            (pending_before || stateful_dispatch_handled)) {
          suppress_input_chord(suppressed_chords, chord);
        }
        return handled;
      },
      [&](std::span<const QuaderWheelInputEdge>) {
        if (state.active_tool == ViewportToolId::LinearArray) {
          finish_array_wheel_event(
              wheel_event_resolved_linear_array_action,
              linear_array_wheel_remainder);
        } else if (state.active_tool == ViewportToolId::RadialArray) {
          finish_array_wheel_event(
              wheel_event_resolved_radial_array_action,
              radial_array_wheel_remainder);
        }
        wheel_event_resolved_linear_array_action = false;
        wheel_event_resolved_radial_array_action = false;
      });

  for (const QuaderInputChord &chord : input.active_input_chords) {
    if (input_chord_is_suppressed(suppressed_chords, chord)) {
      continue;
    }
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_action(editor, state, input, chord, action)) {
      mark_action_down(input, action);
    }
  }
  for (const QuaderInputChord &chord : input.released_input_chords) {
    if (release_suppressed_input_chord(suppressed_chords, chord)) {
      continue;
    }
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_action(editor, state, input, chord, action)) {
      mark_action_released(input, action);
    }
  }
  sync_fly_direction(input);
}

} // namespace quader

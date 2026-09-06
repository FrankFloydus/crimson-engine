////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/input/viewport_keybinding_resolver.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/viewport_input_types.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace quader {
namespace {

bool ordered_pointer_navigation_action(ViewportInputActionKind action) {
  return action == ViewportInputActionKind::Orbit ||
         action == ViewportInputActionKind::OrbitSnap ||
         action == ViewportInputActionKind::Pan ||
         action == ViewportInputActionKind::Fly;
}

void record_navigation_pointer_transition(
    ViewportNavigationInput &navigation,
    ViewportNavigationPointerTransitionKind kind,
    ViewportInputActionKind action,
    const std::optional<ViewportPointerButtonEdge> &pointer_edge) {
  if (!ordered_pointer_navigation_action(action) ||
      !pointer_edge.has_value()) {
    return;
  }
  navigation.pointer_transitions.push_back({
      .kind = kind,
      .action = action,
      .pointer_edge = *pointer_edge,
  });
}

void mark_navigation_pressed(ViewportNavigationInput &navigation,
                             ViewportInputActionKind action,
                             std::optional<ViewportPointerButtonEdge>
                                 pointer_edge) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_pressed = true;
    navigation.orbit_down = true;
    navigation.orbit_pressed_pointer_edge = pointer_edge;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_pressed = true;
    navigation.orbit_snap_down = true;
    navigation.orbit_snap_pressed_pointer_edge = pointer_edge;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_pressed = true;
    navigation.pan_down = true;
    navigation.pan_pressed_pointer_edge = pointer_edge;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_pressed = true;
    navigation.fly_down = true;
    navigation.fly_pressed_pointer_edge = pointer_edge;
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
  record_navigation_pointer_transition(
      navigation, ViewportNavigationPointerTransitionKind::Pressed, action,
      pointer_edge);
}

void mark_action_pressed(ViewportInputFrame &input,
                         ViewportInputActionKind action,
                         std::optional<ViewportPointerButtonEdge>
                             pointer_edge) {
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
  case ViewportInputActionKind::TransferFaceTexture:
    input.transfer_face_texture_pressed = true;
    break;
  case ViewportInputActionKind::ToggleCurvePointSmoothness:
    // Ordered Curve conversion events retain both semantic intent and exact
    // pointer provenance; there is deliberately no parallel aggregate flag.
    break;
  case ViewportInputActionKind::CycleSelectionDepth:
    input.cycle_selection_depth_pressed = true;
    input.cycle_selection_depth_modifiers = {};
    break;
  case ViewportInputActionKind::AddSelectionDepth:
    input.cycle_selection_depth_pressed = true;
    input.cycle_selection_depth_modifiers = {.shift = true};
    break;
  case ViewportInputActionKind::RemoveSelectionDepth:
    input.cycle_selection_depth_pressed = true;
    input.cycle_selection_depth_modifiers = {.primary = true};
    break;
  default:
    mark_navigation_pressed(input.navigation_bindings, action, pointer_edge);
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
  } else if (action == ViewportInputActionKind::TransformVertexSnap) {
    input.transform_vertex_snap_down = true;
  } else {
    mark_navigation_down(input.navigation_bindings, action);
  }
}

void mark_navigation_released(ViewportNavigationInput &navigation,
                              ViewportInputActionKind action,
                              std::optional<ViewportPointerButtonEdge>
                                  pointer_edge) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_released = true;
    navigation.orbit_released_pointer_edge = pointer_edge;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_released = true;
    navigation.orbit_snap_released_pointer_edge = pointer_edge;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_released = true;
    navigation.pan_released_pointer_edge = pointer_edge;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_released = true;
    navigation.fly_released_pointer_edge = pointer_edge;
    break;
  default:
    break;
  }
  record_navigation_pointer_transition(
      navigation, ViewportNavigationPointerTransitionKind::Released, action,
      pointer_edge);
}

void mark_action_released(ViewportInputFrame &input,
                          ViewportInputActionKind action,
                          std::optional<ViewportPointerButtonEdge>
                              pointer_edge) {
  if (action == ViewportInputActionKind::QuickPivot) {
    input.quick_pivot_released = true;
  } else {
    mark_navigation_released(input.navigation_bindings, action,
                             pointer_edge);
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
      .orthographic_view = input.orthographic_view,
      .navigation_active = input.navigation_active,
      .fly_navigation_active = input.fly_navigation_active,
      .orbit_navigation_active = input.orbit_navigation_active,
      .pan_navigation_active = input.pan_navigation_active,
      .modal_tool_active = input.modal_tool_active,
      .active_tool_owns_viewport = input.active_tool_owns_viewport,
      .active_transform_drag = input.active_transform_drag,
      .operation_preview_active = input.operation_preview_active,
      .curve_controls_editable = input.curve_controls_editable,
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
         action == ViewportInputActionKind::ApplyFaceMaterialToMesh ||
         action == ViewportInputActionKind::TransferFaceTexture;
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

bool is_selection_depth_action(ViewportInputActionKind action) {
  return action == ViewportInputActionKind::CycleSelectionDepth ||
         action == ViewportInputActionKind::AddSelectionDepth ||
         action == ViewportInputActionKind::RemoveSelectionDepth;
}

void suppress_input_chord(std::vector<QuaderInputChord> &suppressed,
                          const QuaderInputChord &chord);

void record_curve_conversion_trigger(ViewportInputFrame &input,
                                     ViewportInputActionKind action,
                                     const QuaderInputChord &chord,
                                     std::optional<ViewportPointerButtonEdge>
                                         pointer_edge) {
  if (action == ViewportInputActionKind::ToggleCurvePointSmoothness) {
    input.curve_conversion_edges.push_back({
        .kind = ViewportCurveConversionEdgeKind::Trigger,
        .semantic_chord = chord,
        .pointer_edge = pointer_edge,
    });
  }
}

void record_face_texture_transfer_trigger(
    ViewportInputFrame &input, ViewportInputActionKind action,
    const QuaderInputChord &chord,
    std::optional<ViewportPointerButtonEdge> pointer_edge) {
  if (action == ViewportInputActionKind::TransferFaceTexture &&
      chord.device == QuaderInputDevice::Mouse &&
      chord.mouse_button != QuaderMouseButton::None) {
    input.transfer_face_texture_edges.push_back({
        .kind = ViewportSemanticPointerEdgeKind::Trigger,
        .semantic_chord = chord,
        .pointer_edge = pointer_edge,
    });
  }
}

bool owns_deferred_pointer(ViewportInputActionKind action) {
  return action == ViewportInputActionKind::TransferFaceTexture ||
         action == ViewportInputActionKind::ToggleCurvePointSmoothness;
}

void suppress_owned_pointer_navigation_drag(
    ViewportInputActionKind action, const QuaderInputChord &chord,
    std::vector<QuaderInputChord> &suppressed_chords) {
  if (!owns_deferred_pointer(action) ||
      chord.device != QuaderInputDevice::Mouse ||
      (chord.gesture != QuaderInputGesture::Press &&
       chord.gesture != QuaderInputGesture::DoubleClick) ||
      (chord.mouse_button != QuaderMouseButton::Middle &&
       chord.mouse_button != QuaderMouseButton::Right)) {
    return;
  }
  QuaderInputChord drag = chord;
  drag.gesture = QuaderInputGesture::Drag;
  suppress_input_chord(suppressed_chords, drag);
}

bool reserve_action_pointer(ViewportInputFrame &input,
                            ViewportInputActionKind action,
                            const QuaderInputChord &chord,
                            std::optional<ViewportPointerButtonEdge>
                                pointer_edge) {
  switch (action) {
  case ViewportInputActionKind::TransferFaceTexture:
    if (chord.device != QuaderInputDevice::Mouse ||
        chord.mouse_button == QuaderMouseButton::None) {
      return false;
    }
    input.transfer_face_texture_pointer_reserved = true;
    input.transfer_face_texture_edges.push_back({
        .kind = ViewportSemanticPointerEdgeKind::ReservePress,
        .semantic_chord = chord,
        .pointer_edge = pointer_edge,
    });
    return true;
  case ViewportInputActionKind::ToggleCurvePointSmoothness:
    input.curve_conversion_edges.push_back({
        .kind = ViewportCurveConversionEdgeKind::ReservePress,
        .semantic_chord = chord,
        .pointer_edge = pointer_edge,
    });
    return true;
  default:
    return false;
  }
}

void reserve_deferred_action_pointer(
    QuaderEditorApi &editor, const ViewportToolState &state,
    ViewportInputFrame &input, const QuaderInputChord &chord, bool handled,
    std::optional<ViewportPointerButtonEdge> pointer_edge,
    std::vector<QuaderInputChord> &suppressed_chords) {
  if (handled || input.keybinding_input_consumed ||
      chord.device != QuaderInputDevice::Mouse ||
      (chord.gesture != QuaderInputGesture::Press &&
       chord.gesture != QuaderInputGesture::DoubleClick)) {
    return;
  }
  // Qt substitutes DoubleClick for its second physical Press and omits a
  // second Click on release. An explicit double binding already won above;
  // otherwise preserve the semantic action's ownership of that press.
  if (chord.gesture == QuaderInputGesture::DoubleClick) {
    for (const QuaderInputGesture gesture : {QuaderInputGesture::Press,
                                           QuaderInputGesture::Click}) {
      QuaderInputChord candidate = chord;
      candidate.gesture = gesture;
      ViewportInputActionKind action = ViewportInputActionKind::None;
      if (!resolve_action(editor, state, input, candidate, action)) {
        continue;
      }
      if (gesture == QuaderInputGesture::Press &&
          owns_deferred_pointer(action)) {
        record_curve_conversion_trigger(input, action, candidate,
                                        pointer_edge);
        record_face_texture_transfer_trigger(input, action, candidate,
                                             pointer_edge);
        suppress_owned_pointer_navigation_drag(action, candidate,
                                               suppressed_chords);
        mark_action_pressed(input, action, pointer_edge);
        return;
      }
      if (reserve_action_pointer(input, action, candidate, pointer_edge)) {
        if (gesture == QuaderInputGesture::Press) {
          mark_action_pressed(input, action, pointer_edge);
        }
        return;
      }
    }
    return;
  }
  for (const QuaderInputGesture gesture : {QuaderInputGesture::Click,
                                         QuaderInputGesture::DoubleClick}) {
    QuaderInputChord candidate = chord;
    candidate.gesture = gesture;
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_action(editor, state, input, candidate, action) &&
        reserve_action_pointer(input, action, candidate, pointer_edge)) {
      // Middle/Right emit their navigation Drag beside this physical Press.
      // A deferred semantic action owns that press through the matching
      // release.
      suppress_owned_pointer_navigation_drag(action, chord,
                                             suppressed_chords);
      return;
    }
  }
}

constexpr std::size_t kMouseButtonCount =
    static_cast<std::size_t>(QuaderMouseButton::Button8) + 1u;

/// Tracks raw button-edge positions while semantic chords are dispatched.
struct PointerEdgeProvenanceCursor {
  std::array<std::size_t, kMouseButtonCount> pressed_scan{};
  std::array<std::size_t, kMouseButtonCount> released_scan{};
  // Released semantic actions are dispatched after pressed Click actions.
  // Keep an independent scan so both phases can reference the same physical
  // release edge without stealing provenance from one another.
  std::array<std::size_t, kMouseButtonCount> released_action_scan{};
  std::array<std::optional<ViewportPointerButtonEdge>, kMouseButtonCount>
      last_press{};
};

std::optional<ViewportPointerButtonEdge> consume_pointer_edge(
    std::span<const ViewportPointerButtonEdge> edges, QuaderMouseButton button,
    std::array<std::size_t, kMouseButtonCount> &scan_positions) {
  const std::size_t button_index = static_cast<std::size_t>(button);
  if (button == QuaderMouseButton::None ||
      button_index >= scan_positions.size()) {
    return std::nullopt;
  }
  std::size_t &scan = scan_positions[button_index];
  while (scan < edges.size()) {
    const ViewportPointerButtonEdge edge = edges[scan++];
    if (edge.button == button) {
      return edge;
    }
  }
  return std::nullopt;
}

std::optional<ViewportPointerButtonEdge> pointer_edge_provenance(
    const ViewportInputFrame &input, const QuaderInputChord &chord,
    PointerEdgeProvenanceCursor &cursor) {
  if (chord.device != QuaderInputDevice::Mouse ||
      chord.mouse_button == QuaderMouseButton::None) {
    return std::nullopt;
  }
  const std::size_t button_index =
      static_cast<std::size_t>(chord.mouse_button);
  if (button_index >= cursor.last_press.size()) {
    return std::nullopt;
  }
  switch (chord.gesture) {
  case QuaderInputGesture::Press:
  case QuaderInputGesture::DoubleClick: {
    auto edge = consume_pointer_edge(input.pointer_pressed_edges,
                                     chord.mouse_button,
                                     cursor.pressed_scan);
    cursor.last_press[button_index] = edge;
    return edge;
  }
  case QuaderInputGesture::Click:
    return consume_pointer_edge(input.pointer_released_edges,
                                chord.mouse_button,
                                cursor.released_scan);
  case QuaderInputGesture::Drag:
    // Qt emits a Drag chord immediately after the matching Middle/Right Press.
    // It shares that press rather than consuming the next physical edge.
    return cursor.last_press[button_index];
  default:
    return std::nullopt;
  }
}

std::optional<ViewportPointerButtonEdge> released_pointer_edge_provenance(
    const ViewportInputFrame &input, const QuaderInputChord &chord,
    PointerEdgeProvenanceCursor &cursor) {
  if (chord.device != QuaderInputDevice::Mouse ||
      chord.mouse_button == QuaderMouseButton::None) {
    return std::nullopt;
  }
  return consume_pointer_edge(input.pointer_released_edges,
                              chord.mouse_button,
                              cursor.released_action_scan);
}

bool has_left_mouse_depth_double_click_binding(
    QuaderEditorApi &editor, const ViewportToolState &state,
    const ViewportInputFrame &input, const QuaderInputChord &click) {
  const auto paired_action =
      paired_mouse_double_click_action(editor, state, input, click);
  if (paired_action && is_selection_depth_action(*paired_action)) {
    return true;
  }

  // Modifiers can change between the first click and Qt's recognized double.
  // These target lookups use the effective-rule cache, not the Settings catalog.
  for (const std::string_view target : {
           "viewport.selection.cycle_depth", "viewport.selection.add_depth",
           "viewport.selection.remove_depth"}) {
    const auto mapping =
        quader_input_parse_shortcut(editor.commands().shortcut_for(target));
    if (!mapping || mapping->size() != 1u) {
      continue;
    }
    const QuaderInputChord &candidate = mapping->front();
    if (candidate.device != QuaderInputDevice::Mouse ||
        candidate.mouse_button != QuaderMouseButton::Left ||
        candidate.gesture != QuaderInputGesture::DoubleClick) {
      continue;
    }
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_action(editor, state, input, candidate, action) &&
        is_selection_depth_action(action)) {
      return true;
    }
  }
  return false;
}

void record_selection_click_provenance(
    QuaderEditorApi &editor, const ViewportToolState &state,
    ViewportInputFrame &input, const QuaderInputChord &chord, bool handled) {
  if (chord.device != QuaderInputDevice::Mouse ||
      chord.mouse_button != QuaderMouseButton::Left ||
      (chord.gesture != QuaderInputGesture::Press &&
       chord.gesture != QuaderInputGesture::Click)) {
    return;
  }

  input.selection_click_delay_for_double_click = false;
  input.selection_click_modifiers = {};
  if (handled || input.keybinding_input_consumed) {
    return;
  }

  QuaderInputChord click = chord;
  click.gesture = QuaderInputGesture::Click;
  if (!has_left_mouse_depth_double_click_binding(editor, state, input, click)) {
    return;
  }
  input.selection_click_delay_for_double_click = true;
  input.selection_click_modifiers = {
      .primary = chord.primary, .shift = chord.shift, .alt = chord.alt};
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
  case QuaderViewportKeybindingDispatch::Kind::ViewportInputAction:
    // Semantic actions need edge provenance and wheel magnitude, so the
    // ordered-edge caller applies this result after the shared dispatcher has
    // selected it.
    return false;
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

void ViewportKeybindingResolver::resolve(
    QuaderEditorApi &editor, const ViewportToolState &state,
    ViewportInputFrame &input,
    std::vector<QuaderInputChord> &suppressed_chords,
    double &linear_array_wheel_remainder,
    double &radial_array_wheel_remainder,
    ViewportRuntimeBindingToken runtime_binding) {
  input.resolved_shortcuts.clear();
  input.navigation_bindings = {};
  input.transform_vertex_snap_down = false;
  input.linear_array_copy_delta = 0;
  input.radial_array_copy_delta = 0;
  input.sample_face_material_pressed = false;
  input.apply_active_material_to_selection_pressed = false;
  input.apply_active_material_delay_for_double_click = false;
  input.apply_face_material_to_mesh_pressed = false;
  input.transfer_face_texture_pressed = false;
  input.transfer_face_texture_pointer_reserved = false;
  input.transfer_face_texture_edges.clear();
  input.curve_conversion_edges.clear();
  input.command_pointer_edges.clear();
  input.selection_click_delay_for_double_click = false;
  input.selection_click_modifiers = {};
  input.cycle_selection_depth_pressed = false;
  input.cycle_selection_depth_modifiers = {};
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
    editor.commands().reset_pending_viewport_keybinding_chord();
    suppressed_chords.clear();
    return;
  }

  bool wheel_event_resolved_linear_array_action = false;
  bool wheel_event_resolved_radial_array_action = false;
  std::size_t wheel_input_edge_offset = 0u;
  bool dispatch_continues = runtime_binding.current();
  PointerEdgeProvenanceCursor pointer_edge_cursor;
  dispatch_ordered_viewport_input_edges(
      input.pressed_input_chords, input.wheel_input_edges,
      [&](const QuaderInputChord &chord, ViewportInputEdgeKind kind,
          double wheel_step_delta) {
        if (!dispatch_continues) {
          return true;
        }
        // Consume provenance before suppression or action lookup so an
        // unbound same-button chord cannot shift a later bound event onto the
        // wrong raw pointer position.
        const auto pointer_edge =
            kind == ViewportInputEdgeKind::Pressed
                ? pointer_edge_provenance(input, chord, pointer_edge_cursor)
                : std::nullopt;
        if (pointer_edge && chord.device == QuaderInputDevice::Mouse &&
            (chord.gesture == QuaderInputGesture::Press ||
             chord.gesture == QuaderInputGesture::DoubleClick)) {
          // A new physical press ends any prior reservation of this button,
          // including an earlier complete click in the same host frame.
          std::erase_if(suppressed_chords, [&](const auto &suppressed) {
            return suppressed.device == QuaderInputDevice::Mouse &&
                   suppressed.mouse_button == chord.mouse_button;
          });
        }
        const bool command_pointer_reserved = pointer_edge &&
            chord.device == QuaderInputDevice::Mouse &&
            std::any_of(suppressed_chords.begin(), suppressed_chords.end(),
                        [&](const auto &suppressed) {
              return suppressed.device == QuaderInputDevice::Mouse &&
                     suppressed.mouse_button == chord.mouse_button &&
                     (suppressed.gesture == QuaderInputGesture::Press ||
                      suppressed.gesture == QuaderInputGesture::DoubleClick);
            });
        if (command_pointer_reserved) {
          // A command/prefix owns the following Click/Drag too, independently
          // of modifiers. It must not become a second command or navigation.
          input.command_pointer_edges.push_back({
              .kind = ViewportSemanticPointerEdgeKind::Trigger,
              .semantic_chord = chord,
              .pointer_edge = pointer_edge,
          });
          return true;
        }
        if (kind == ViewportInputEdgeKind::Pressed &&
            input_chord_is_suppressed(suppressed_chords, chord)) {
          return true;
        }
        const bool pending_before =
            editor.commands().has_pending_keybinding_chord();
        bool stateful_dispatch_handled = false;
        bool stateful_dispatch_suppresses_chord = false;
        const auto apply_semantic_action =
            [&](const QuaderInputChord &candidate,
                ViewportInputEdgeKind edge_kind,
                ViewportInputActionKind action) {
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
                    const float wheel_steps =
                        static_cast<float>(wheel_step_delta);
                    QVec2 event_local = input.mouse_local;
                    if (wheel_input_edge_offset <
                        input.wheel_input_edges.size()) {
                      const QuaderWheelInputEdge &physical_edge =
                          input.wheel_input_edges[wheel_input_edge_offset];
                      if (physical_edge.has_local_position) {
                        event_local = {
                            physical_edge.local.x,
                            physical_edge.local.y,
                        };
                      }
                    }
                    input.mouse_wheel += wheel_steps;
                    input.navigation_bindings.wheel_events.push_back({
                        .action = action,
                        .steps = wheel_steps,
                        .local = event_local,
                    });
                  }
                }
              } else {
                record_material_action_provenance(editor, state, input,
                                                  candidate, action);
                record_face_texture_transfer_trigger(input, action, candidate,
                                                     pointer_edge);
                record_curve_conversion_trigger(input, action, candidate,
                                                pointer_edge);
                suppress_owned_pointer_navigation_drag(action, candidate,
                                                       suppressed_chords);
                mark_action_pressed(input, action, pointer_edge);
              }
              return true;
            };
        const bool handled = dispatch_viewport_input_edge(
            chord, kind,
            [&]() { return pending_before; },
            [](const QuaderInputChord &, ViewportInputEdgeKind) {
              // Pressed and wheel edges have one resolver: the stateful
              // keybinding dispatcher below. Stateless semantic lookup is
              // intentionally limited to deferred gesture probes and
              // active/released chords.
              return false;
            },
            [&](const QuaderInputChord &candidate,
                ViewportInputEdgeKind edge_kind) {
              const QuaderViewportKeybindingDispatch dispatch =
                  editor.commands().dispatch_viewport_input_binding(
                      candidate, state, input.viewport_focused,
                      input.text_input_active, keybinding_context(input));
              if (dispatch.kind == QuaderViewportKeybindingDispatch::Kind::
                                       ViewportInputAction) {
                stateful_dispatch_handled =
                    apply_semantic_action(candidate, edge_kind,
                                          dispatch.action);
                return stateful_dispatch_handled;
              }
              stateful_dispatch_handled =
                  apply_keybinding_dispatch(editor, dispatch, input);
              stateful_dispatch_suppresses_chord =
                  stateful_dispatch_handled;
              return stateful_dispatch_handled;
            });
        if (!runtime_binding.current()) {
          dispatch_continues = false;
          return true;
        }
        if (pending_before) {
          input.keybinding_input_consumed = true;
        }
        if (kind == ViewportInputEdgeKind::Pressed) {
          reserve_deferred_action_pointer(editor, state, input, chord, handled,
                                          pointer_edge, suppressed_chords);
          record_selection_click_provenance(editor, state, input, chord,
                                            handled);
        }
        if (kind == ViewportInputEdgeKind::Pressed &&
            (pending_before || stateful_dispatch_suppresses_chord)) {
          suppress_input_chord(suppressed_chords, chord);
          if (chord.device == QuaderInputDevice::Mouse && pointer_edge) {
            input.command_pointer_edges.push_back({
                .kind = chord.gesture == QuaderInputGesture::Click
                            ? ViewportSemanticPointerEdgeKind::Trigger
                            : ViewportSemanticPointerEdgeKind::ReservePress,
                .semantic_chord = chord,
                .pointer_edge = pointer_edge,
            });
          }
        }
        return handled;
      },
      [&](std::span<const QuaderWheelInputEdge> physical_event) {
        wheel_input_edge_offset += physical_event.size();
        if (!dispatch_continues) {
          return;
        }
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

  if (!dispatch_continues) {
    return;
  }

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
    const auto pointer_edge =
        released_pointer_edge_provenance(input, chord,
                                         pointer_edge_cursor);
    if (release_suppressed_input_chord(suppressed_chords, chord)) {
      continue;
    }
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_action(editor, state, input, chord, action)) {
      mark_action_released(input, action, pointer_edge);
    }
  }
  // Qt carries ordinary button-up as a pointer edge, not a released Press
  // chord. Retire its reservation only after resolving this frame's Click.
  for (const auto &release : input.pointer_released_edges) {
    const auto *press =
        viewport_pointer_latest_edge(input.pointer_pressed_edges, release.button);
    if (press && press->sequence != 0 && release.sequence != 0 &&
        press->sequence > release.sequence) continue;
    std::erase_if(suppressed_chords, [&](const auto &suppressed) {
      return suppressed.device == QuaderInputDevice::Mouse &&
             suppressed.mouse_button == release.button;
    });
  }
  std::stable_sort(
      input.navigation_bindings.pointer_transitions.begin(),
      input.navigation_bindings.pointer_transitions.end(),
      [](const ViewportNavigationPointerTransition &lhs,
         const ViewportNavigationPointerTransition &rhs) {
        const std::uint64_t lhs_sequence = lhs.pointer_edge.sequence;
        const std::uint64_t rhs_sequence = rhs.pointer_edge.sequence;
        if (lhs_sequence == 0u || rhs_sequence == 0u) {
          return lhs_sequence != 0u && rhs_sequence == 0u;
        }
        return lhs_sequence < rhs_sequence;
      });
  sync_fly_direction(input);
}

} // namespace quader

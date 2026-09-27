////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/transform_operation_input_hints.hpp"

namespace quader {
namespace {

using Gesture = QuaderOperationInputGesture;
using Handle = QuaderInputTransformHandle;
using Operation = QuaderInputTransformOperation;
using Hints = std::vector<QuaderOperationInputHint>;

QuaderOperationInputHint binding(std::string_view target, std::string_view key,
                                 std::string_view fallback,
                                 std::uint8_t priority = 60) {
  return {Gesture::Keybinding, QuaderCommandId{target}, key, fallback, priority};
}

QuaderOperationInputHint gesture(Gesture input, std::string_view key,
                                 std::string_view fallback, bool active = false,
                                 std::uint8_t priority = 70) {
  return {input, {}, key, fallback, priority, active};
}

void append_completion(Hints &hints, bool can_apply,
                       std::string_view cancel_key,
                       std::string_view cancel_label,
                       std::string_view apply_target = "viewport.modal_tool.finalize") {
  if (can_apply) {
    hints.push_back(binding(apply_target, "operation.hints.apply", "Apply", 90));
  }
  hints.push_back(binding("viewport.cancel_active_tool", cancel_key,
                          cancel_label, 90));
}

void append_grid_snap(Hints &hints,
                       const QuaderOperationInputContext &context) {
  if (!context.grid_snap_available) {
    return;
  }
  hints.push_back(gesture(
      Gesture::Control,
      context.grid_snap_enabled ? "operation.hints.snap.grid_disable"
                                : "operation.hints.snap.grid_enable",
      context.grid_snap_enabled ? "Disable Grid Snap" : "Enable Grid Snap",
      context.control));
}

void append_rotation_snap(Hints &hints,
                           const QuaderOperationInputContext &context) {
  const bool trackball = context.transform_handle == Handle::Trackball;
  if (!context.angle_snap_available) {
    if (trackball) {
      hints.push_back(gesture(Gesture::Control, "operation.hints.precision",
                               "Precision", context.control));
    }
    return;
  }
  if (trackball) {
    hints.push_back(gesture(
        Gesture::Control,
        context.angle_snap_enabled ? "operation.hints.snap.precision_angle_disable"
                                   : "operation.hints.snap.precision_angle_enable",
        context.angle_snap_enabled ? "Precision + Disable Angle Snap"
                                   : "Precision + Enable Angle Snap",
        context.control));
  } else {
    hints.push_back(gesture(
        Gesture::Control,
        context.angle_snap_enabled ? "operation.hints.snap.angle_disable"
                                   : "operation.hints.snap.angle_enable",
        context.angle_snap_enabled ? "Disable Angle Snap" : "Enable Angle Snap",
        context.control));
  }
}

void append_transform_cancel(Hints &hints, std::string_view tool_id,
                              Operation operation) {
  std::string_view key = "operation.hints.transform.cancel_move";
  std::string_view label = "Cancel Move";
  if (operation == Operation::Duplicate) {
    key = "operation.hints.transform.cancel_duplicate";
    label = "Cancel Duplication";
  } else if (operation == Operation::Extrude) {
    key = "operation.hints.extrude.cancel";
    label = "Cancel Extrusion";
  } else if (operation == Operation::Inset) {
    key = "operation.hints.transform.cancel_inset";
    label = "Cancel Inset";
  } else if (operation == Operation::Rotate || tool_id == "rotate") {
    key = "operation.hints.transform.cancel_rotate";
    label = "Cancel Rotation";
  } else if (operation == Operation::Scale || tool_id == "scale") {
    key = "operation.hints.transform.cancel_scale";
    label = "Cancel Scale";
  }
  hints.push_back(binding("viewport.cancel_active_tool", key, label, 90));
}

void append_transform(Hints &hints, std::string_view tool_id,
                        const QuaderOperationInputContext &context) {
  if (context.transform_handle == Handle::None) {
    return;
  }
  const bool move = tool_id == "move" || tool_id == "curve";
  if (context.pointer_dragging) {
    append_transform_cancel(hints, tool_id, context.transform_operation);
  } else if (context.can_duplicate) {
    hints.push_back(gesture(Gesture::ShiftAltPrimaryDrag,
                             "operation.hints.transform.duplicate",
                             "Duplicate and Transform",
                             context.shift || context.alt));
  } else if (context.can_component_inset && tool_id == "scale") {
    hints.push_back(gesture(Gesture::ShiftPrimaryDrag,
                             "operation.hints.transform.inset", "Inset",
                             context.shift));
  } else if (context.can_component_extrude &&
             (tool_id == "move" || tool_id == "rotate")) {
    hints.push_back(gesture(Gesture::ShiftPrimaryDrag,
                             "operation.hints.transform.extrude", "Extrude",
                             context.shift));
  }
  if (tool_id == "rotate") {
    append_rotation_snap(hints, context);
  } else {
    append_grid_snap(hints, context);
  }
  const bool snap_handle = context.transform_handle == Handle::Axis ||
                           context.transform_handle == Handle::Center;
  if (move && snap_handle && context.can_vertex_snap &&
      (tool_id != "curve" || context.curve_anchor_selection_only)) {
    auto hint = binding("viewport.transform.vertex_snap",
                        "operation.hints.transform.vertex_snap", "Vertex Snap", 70);
    hint.active = context.held_vertex_snap;
    hints.push_back(hint);
  }
}

void append_axes(Hints &hints) {
  auto hint = binding("viewport.linear_array.constraint_x",
                      "operation.hints.axis_constraint", "Axis Constraint");
  hint.related_commands[0] = QuaderCommandId{"viewport.linear_array.constraint_y"};
  hint.related_commands[1] = QuaderCommandId{"viewport.linear_array.constraint_z"};
  hint.related_command_count = 2;
  hints.push_back(hint);
}

void append_pair(Hints &hints, std::string_view increase,
                   std::string_view decrease, std::string_view key,
                   std::string_view fallback, bool active) {
  auto hint = binding(increase, key, fallback);
  hint.related_commands[0] = QuaderCommandId{decrease};
  hint.related_command_count = 1;
  hint.active = active;
  hints.push_back(hint);
}

} // namespace

std::vector<QuaderOperationInputHint> transform_operation_input_hints(
    std::string_view tool_id, const TransformOperationHintState &state,
    const QuaderOperationInputContext &context) {
  Hints hints;
  if (context.quick_pivot) {
    auto hint = binding("viewport.quick_pivot", "operation.hints.pivot.quick",
                        "Edit Pivot", 90);
    hint.gesture = Gesture::KeybindingHold;
    hint.active = true;
    hints.push_back(hint);
    return hints;
  }
  if (tool_id == "move" || tool_id == "rotate" || tool_id == "scale") {
    append_transform(hints, tool_id, context);
  } else if (tool_id == "pivot") {
    append_completion(hints, state.can_apply,
                        "operation.hints.pivot.cancel", "Cancel Pivot");
  } else if (tool_id == "linear_array") {
    append_completion(hints, state.can_apply,
                        "operation.hints.array.cancel", "Cancel Array");
    append_axes(hints);
    append_pair(hints, "viewport.linear_array.increase_copies",
                   "viewport.linear_array.decrease_copies",
                   "operation.hints.array.copies", "Copies", context.shift);
    if (state.owner_dragging || context.linear_array_endpoint_hovered) {
      if (state.linear_array_unconstrained) {
        hints.push_back(gesture(Gesture::Alt,
                                 "operation.hints.array.move_vertically",
                                 "Move Vertically", context.alt));
      }
      append_grid_snap(hints, context);
    }
  } else if (tool_id == "radial_array") {
    append_completion(hints, state.can_apply,
                        "operation.hints.array.cancel", "Cancel Array",
                        "viewport.radial_array.finalize");
    if (!state.radial_center_locked) {
      hints.push_back(gesture(Gesture::PrimaryClick,
                               "operation.hints.radial_array.choose_center",
                               "Choose Center"));
    }
    append_pair(hints, "viewport.radial_array.increase_copies",
                   "viewport.radial_array.decrease_copies",
                   "operation.hints.array.copies", "Copies", context.shift);
    append_pair(hints, "viewport.radial_array.increase_repeats",
                   "viewport.radial_array.decrease_repeats",
                   "operation.hints.array.repeats", "Repeats", context.control);
    if (state.instances_available) {
      hints.push_back(binding("viewport.radial_array.toggle_instances",
                               "operation.hints.array.instances", "Instances", 40));
    }
  } else if (tool_id == "path_array") {
    append_completion(hints, state.can_apply,
                        "operation.hints.array.cancel", "Cancel Array");
    if (context.path_source_hovered) {
      hints.push_back(gesture(
          Gesture::PrimaryClick,
          state.path_has_source ? "operation.hints.path_array.replace_source"
                                : "operation.hints.path_array.choose_source",
          state.path_has_source ? "Replace Source" : "Choose Source"));
    }
  } else if (tool_id == "curve") {
    const bool control_gizmo = context.transform_handle != Handle::None &&
                               !state.curve_construction_dragging;
    if (control_gizmo) {
      auto control_context = context;
      control_context.pointer_dragging |= state.owner_dragging;
      append_transform(hints, tool_id, control_context);
    } else if (state.owner_dragging && !state.curve_construction_dragging) {
      append_transform_cancel(hints, tool_id, Operation::Move);
    }
    if (state.curve_drawing && !context.pointer_dragging &&
        !state.owner_dragging) {
      append_completion(hints, state.can_apply && !state.owner_dragging,
                          "operation.hints.curve.cancel", "Cancel Curve");
      if (!state.owner_dragging && !control_gizmo &&
          !context.curve_anchor_hovered && !context.curve_tangent_hovered) {
        hints.push_back(gesture(
            state.curve_preset ? Gesture::PrimaryClick : Gesture::PrimaryDrag,
            state.curve_preset ? "operation.hints.curve.place_shape"
                               : "operation.hints.curve.shape_tangents",
            state.curve_preset ? "Place Curve" : "Shape Tangents"));
      }
    } else if (state.curve_construction_dragging) {
      append_completion(hints, false, "operation.hints.curve.cancel",
                          "Cancel Curve");
    }
    if (!context.pointer_dragging && !state.owner_dragging &&
        context.curve_anchor_hovered && !context.curve_tangent_hovered) {
      auto hint = binding("viewport.curve.toggle_point_smoothness",
                          "operation.hints.curve.corner_smooth",
                          "Toggle Corner / Smooth", 70);
      hint.active = context.alt;
      hints.push_back(hint);
    }
  }
  return hints;
}

} // namespace quader

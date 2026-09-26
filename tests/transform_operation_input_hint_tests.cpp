////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/transform_operation_input_hints.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>

namespace quader {
namespace {

using Gesture = QuaderOperationInputGesture;
using Handle = QuaderInputTransformHandle;
using Operation = QuaderInputTransformOperation;
using Hints = std::vector<QuaderOperationInputHint>;

const QuaderOperationInputHint *find_target(const Hints &hints,
                                           std::string_view target) {
  const auto found = std::find_if(hints.begin(), hints.end(), [&](const auto &hint) {
    if (hint.command.string() == target) {
      return true;
    }
    return std::any_of(hint.related_commands.begin(),
                       hint.related_commands.begin() + hint.related_command_count,
                       [&](const auto command) {
                         return command.string() == target;
                       });
  });
  return found == hints.end() ? nullptr : &*found;
}

const QuaderOperationInputHint *find_gesture(const Hints &hints, Gesture gesture) {
  const auto found = std::find_if(hints.begin(), hints.end(), [&](const auto &hint) {
    return hint.gesture == gesture;
  });
  return found == hints.end() ? nullptr : &*found;
}

TEST(TransformOperationInputHintTests, UntargetedTransformHasNoSelectionClutter) {
  QuaderOperationInputContext context;
  for (const auto tool : {"move", "rotate", "scale"}) {
    EXPECT_TRUE(transform_operation_input_hints(tool, {}, context).empty());
    context.shift = true;
    context.control = true;
    EXPECT_TRUE(transform_operation_input_hints(tool, {}, context).empty());
  }
}

TEST(TransformOperationInputHintTests, PressTimeOperationDoesNotChangeDuringDrag) {
  QuaderOperationInputContext context;
  context.transform_handle = Handle::Axis;
  context.can_duplicate = true;
  auto hints = transform_operation_input_hints("move", {}, context);
  EXPECT_NE(find_gesture(hints, Gesture::ShiftAltPrimaryDrag), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.cancel_active_tool"), nullptr);

  context.pointer_dragging = true;
  context.shift = false;
  context.transform_operation = Operation::Duplicate;
  hints = transform_operation_input_hints("move", {}, context);
  EXPECT_EQ(find_gesture(hints, Gesture::ShiftAltPrimaryDrag), nullptr);
  ASSERT_NE(find_target(hints, "viewport.cancel_active_tool"), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.cancel_active_tool")->label_fallback,
            "Cancel Duplication");
  EXPECT_EQ(find_target(hints, "viewport.modal_tool.finalize"), nullptr);

  context.transform_operation = Operation::Move;
  context.shift = true;
  hints = transform_operation_input_hints("move", {}, context);
  EXPECT_EQ(find_target(hints, "viewport.cancel_active_tool")->label_fallback,
            "Cancel Move");
}

TEST(TransformOperationInputHintTests, ComponentActionsRequireOwnerEligibility) {
  QuaderOperationInputContext context;
  context.transform_handle = Handle::Axis;
  EXPECT_EQ(find_gesture(transform_operation_input_hints("move", {}, context),
                         Gesture::ShiftPrimaryDrag), nullptr);
  context.can_component_extrude = true;
  for (const auto tool : {"move", "rotate"}) {
    const auto hints = transform_operation_input_hints(tool, {}, context);
    const auto *hint = find_gesture(hints, Gesture::ShiftPrimaryDrag);
    ASSERT_NE(hint, nullptr);
    EXPECT_EQ(hint->label_fallback, "Extrude");
  }
  EXPECT_EQ(find_gesture(transform_operation_input_hints("scale", {}, context),
                         Gesture::ShiftPrimaryDrag), nullptr);
  context.can_component_inset = true;
  const auto hints = transform_operation_input_hints("scale", {}, context);
  ASSERT_NE(find_gesture(hints, Gesture::ShiftPrimaryDrag), nullptr);
  EXPECT_EQ(find_gesture(hints, Gesture::ShiftPrimaryDrag)->label_fallback, "Inset");
}

TEST(TransformOperationInputHintTests, MoveVertexSnapExcludesPlanesScaleAndRotation) {
  QuaderOperationInputContext context;
  context.can_vertex_snap = true;
  context.held_vertex_snap = true;
  for (const auto handle : {Handle::Axis, Handle::Center}) {
    context.transform_handle = handle;
    const auto hints = transform_operation_input_hints("move", {}, context);
    ASSERT_NE(find_target(hints, "viewport.transform.vertex_snap"), nullptr);
    EXPECT_TRUE(find_target(hints, "viewport.transform.vertex_snap")->active);
    for (const auto tool : {"rotate", "scale"}) {
      EXPECT_EQ(find_target(transform_operation_input_hints(tool, {}, context),
                            "viewport.transform.vertex_snap"), nullptr);
    }
  }
  context.transform_handle = Handle::Plane;
  EXPECT_EQ(find_target(transform_operation_input_hints("move", {}, context),
                        "viewport.transform.vertex_snap"), nullptr);
  context.transform_handle = Handle::Axis;
  context.curve_anchor_selection_only = false;
  EXPECT_EQ(find_target(transform_operation_input_hints("curve", {}, context),
                        "viewport.transform.vertex_snap"), nullptr);
  context.curve_anchor_selection_only = true;
  EXPECT_NE(find_target(transform_operation_input_hints("curve", {}, context),
                        "viewport.transform.vertex_snap"), nullptr);
  context.can_vertex_snap = false;
  EXPECT_EQ(find_target(transform_operation_input_hints("curve", {}, context),
                        "viewport.transform.vertex_snap"), nullptr);
}

TEST(TransformOperationInputHintTests, SnapLabelsUseBaseSettingsAndTrackballPrecision) {
  QuaderOperationInputContext context;
  context.transform_handle = Handle::Axis;
  context.grid_snap_enabled = true;
  auto hints = transform_operation_input_hints("move", {}, context);
  ASSERT_NE(find_gesture(hints, Gesture::Control), nullptr);
  EXPECT_EQ(find_gesture(hints, Gesture::Control)->label_fallback, "Disable Grid Snap");
  context.grid_snap_enabled = false;
  context.control = true;
  hints = transform_operation_input_hints("scale", {}, context);
  EXPECT_EQ(find_gesture(hints, Gesture::Control)->label_fallback, "Enable Grid Snap");
  EXPECT_TRUE(find_gesture(hints, Gesture::Control)->active);
  context.grid_snap_available = false;
  EXPECT_EQ(find_gesture(transform_operation_input_hints("move", {}, context),
                         Gesture::Control), nullptr);

  context.angle_snap_enabled = false;
  hints = transform_operation_input_hints("rotate", {}, context);
  EXPECT_EQ(find_gesture(hints, Gesture::Control)->label_fallback, "Enable Angle Snap");
  context.transform_handle = Handle::Trackball;
  hints = transform_operation_input_hints("rotate", {}, context);
  EXPECT_EQ(find_gesture(hints, Gesture::Control)->label_fallback,
            "Precision + Enable Angle Snap");
  context.angle_snap_available = false;
  hints = transform_operation_input_hints("rotate", {}, context);
  EXPECT_EQ(find_gesture(hints, Gesture::Control)->label_fallback, "Precision");
}

TEST(TransformOperationInputHintTests, QuickPivotDoesNotAdvertisePersistentCompletion) {
  TransformOperationHintState state;
  state.can_apply = true;
  QuaderOperationInputContext context;
  auto hints = transform_operation_input_hints("pivot", state, context);
  EXPECT_NE(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  EXPECT_NE(find_target(hints, "viewport.cancel_active_tool"), nullptr);
  context.quick_pivot = true;
  hints = transform_operation_input_hints("move", state, context);
  ASSERT_EQ(hints.size(), 1u);
  EXPECT_EQ(hints.front().command.string(), "viewport.quick_pivot");
  EXPECT_EQ(hints.front().gesture, Gesture::KeybindingHold);
  EXPECT_TRUE(hints.front().active);
}

TEST(TransformOperationInputHintTests, LinearArrayGroupsBindingsAndKeepsDragPending) {
  TransformOperationHintState state;
  state.can_apply = true;
  QuaderOperationInputContext context;
  auto hints = transform_operation_input_hints("linear_array", state, context);
  EXPECT_NE(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  const auto *axis = find_target(hints, "viewport.linear_array.constraint_z");
  ASSERT_NE(axis, nullptr);
  EXPECT_EQ(axis->related_command_count, 2);
  const auto *copies = find_target(hints, "viewport.linear_array.decrease_copies");
  ASSERT_NE(copies, nullptr);
  EXPECT_EQ(copies->related_command_count, 1);
  EXPECT_EQ(find_gesture(hints, Gesture::Control), nullptr);

  state.owner_dragging = true;
  hints = transform_operation_input_hints("linear_array", state, context);
  EXPECT_NE(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  EXPECT_NE(find_target(hints, "viewport.cancel_active_tool"), nullptr);
  EXPECT_NE(find_gesture(hints, Gesture::Control), nullptr);
  state.owner_dragging = false;
  context.linear_array_endpoint_hovered = true;
  EXPECT_NE(find_gesture(transform_operation_input_hints("linear_array", state, context),
                         Gesture::Control), nullptr);
}

TEST(TransformOperationInputHintTests, RadialPlacementAndCanonicalCompletionAreDistinct) {
  TransformOperationHintState state;
  QuaderOperationInputContext context;
  auto hints = transform_operation_input_hints("radial_array", state, context);
  EXPECT_NE(find_gesture(hints, Gesture::PrimaryClick), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.radial_array.finalize"), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.radial_array.toggle_instances"), nullptr);
  EXPECT_NE(find_target(hints, "viewport.radial_array.decrease_repeats"), nullptr);
  state.radial_center_locked = true;
  state.can_apply = true;
  state.instances_available = true;
  hints = transform_operation_input_hints("radial_array", state, context);
  EXPECT_EQ(find_gesture(hints, Gesture::PrimaryClick), nullptr);
  EXPECT_NE(find_target(hints, "viewport.radial_array.finalize"), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  EXPECT_NE(find_target(hints, "viewport.radial_array.toggle_instances"), nullptr);
}

TEST(TransformOperationInputHintTests, PathSourceHintsRequireAnEligibleTarget) {
  TransformOperationHintState state;
  QuaderOperationInputContext context;
  auto hints = transform_operation_input_hints("path_array", state, context);
  EXPECT_EQ(find_gesture(hints, Gesture::PrimaryClick), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  context.path_source_hovered = true;
  hints = transform_operation_input_hints("path_array", state, context);
  EXPECT_EQ(find_gesture(hints, Gesture::PrimaryClick)->label_fallback, "Choose Source");
  state.path_has_source = true;
  state.can_apply = true;
  hints = transform_operation_input_hints("path_array", state, context);
  EXPECT_EQ(find_gesture(hints, Gesture::PrimaryClick)->label_fallback, "Replace Source");
  EXPECT_NE(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
}

TEST(TransformOperationInputHintTests, CurveConstructionExistingControlsAndPresetDiffer) {
  TransformOperationHintState state;
  state.curve_drawing = true;
  QuaderOperationInputContext context;
  auto hints = transform_operation_input_hints("curve", state, context);
  EXPECT_NE(find_gesture(hints, Gesture::PrimaryDrag), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  state.can_apply = true;
  hints = transform_operation_input_hints("curve", state, context);
  EXPECT_NE(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  state.owner_dragging = true;
  state.curve_construction_dragging = true;
  context.pointer_dragging = true;
  hints = transform_operation_input_hints("curve", state, context);
  EXPECT_EQ(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  EXPECT_NE(find_target(hints, "viewport.cancel_active_tool"), nullptr);
  EXPECT_EQ(find_gesture(hints, Gesture::PrimaryDrag), nullptr);

  state.owner_dragging = false;
  state.curve_construction_dragging = false;
  context.pointer_dragging = false;
  state.curve_preset = true;
  hints = transform_operation_input_hints("curve", state, context);
  EXPECT_NE(find_gesture(hints, Gesture::PrimaryClick), nullptr);
  EXPECT_EQ(find_gesture(hints, Gesture::PrimaryDrag), nullptr);

  state.curve_drawing = false;
  context.curve_anchor_hovered = true;
  hints = transform_operation_input_hints("curve", state, context);
  EXPECT_NE(find_target(hints, "viewport.curve.toggle_point_smoothness"), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.modal_tool.finalize"), nullptr);
  context.curve_tangent_hovered = true;
  hints = transform_operation_input_hints("curve", state, context);
  EXPECT_EQ(find_target(hints, "viewport.curve.toggle_point_smoothness"), nullptr);

  state.owner_dragging = true;
  hints = transform_operation_input_hints("curve", state, context);
  ASSERT_NE(find_target(hints, "viewport.cancel_active_tool"), nullptr);
  EXPECT_EQ(find_target(hints, "viewport.cancel_active_tool")->label_fallback,
            "Cancel Move");
}

} // namespace
} // namespace quader

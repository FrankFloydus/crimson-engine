////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/operation_input_hint_catalog.hpp"

namespace quader {
namespace {
using Gesture = QuaderOperationInputGesture;
using Hints = std::vector<QuaderOperationInputHint>;

QuaderOperationInputHint gesture(Gesture input, std::string_view key,
                                 std::string_view label, bool active = false,
                                 std::uint8_t priority = 50) {
  return {input, {}, key, label, priority, active};
}
QuaderOperationInputHint binding(std::string_view target, std::string_view key,
                                 std::string_view label, std::uint8_t priority = 50) {
  return {Gesture::Keybinding, QuaderCommandId{target}, key, label, priority};
}
void completion(Hints &hints, bool can_apply, std::string_view cancel_key,
                std::string_view cancel_label) {
  if (can_apply) {
    hints.push_back(binding("viewport.modal_tool.finalize", "operation.hints.apply", "Apply", 95));
  }
  hints.push_back(binding("viewport.cancel_active_tool", cancel_key, cancel_label, 90));
}
void grid_snap(Hints &hints, const QuaderOperationInputContext &context) {
  if (!context.grid_snap_available) return;
  hints.push_back(gesture(Gesture::Control,
      context.grid_snap_enabled ? "operation.hints.snap.grid_disable" : "operation.hints.snap.grid_enable",
      context.grid_snap_enabled ? "Disable Grid Snap" : "Enable Grid Snap", context.control, 80));
}
QuaderOperationInputHint wheel_pair(std::string_view up, std::string_view down,
                                    std::string_view key, std::string_view label) {
  auto hint = binding(up, key, label);
  hint.related_commands[0] = QuaderCommandId{down};
  hint.related_command_count = 1;
  return hint;
}
} // namespace

std::vector<QuaderOperationInputHint>
selection_gesture_input_hints(const QuaderOperationInputContext &context) {
  using Selection = QuaderInputSelectionGesture;
  Hints hints;
  if (context.selection_gesture == Selection::None) return hints;
  if (context.selection_gesture == Selection::DepthCycle) {
    return {gesture(Gesture::Information, "operation.hints.selection.depth_cycle", "Cycle Depth", true, 95)};
  }
  if (context.selection_gesture == Selection::ExpandComponent) {
    return {gesture(Gesture::Information, "operation.hints.selection.expand", "Expand Selection", true, 95)};
  }
  if (context.selection_gesture == Selection::EnterInstance) {
    return {gesture(Gesture::Information, "operation.hints.selection.enter_instance", "Enter Instance", true, 95)};
  }
  if (context.selection_gesture == Selection::Paint) {
    hints.push_back(gesture(Gesture::Control, "operation.hints.selection.remove", "Remove", context.control, 80));
    return hints;
  }
  if (context.selection_gesture == Selection::EdgeLoop || context.selection_gesture == Selection::EdgeRing) {
    const bool ring = context.selection_gesture == Selection::EdgeRing;
    hints.push_back(gesture(Gesture::Information,
        ring ? (context.selection_toggle ? "operation.hints.selection.toggle_ring" : "operation.hints.selection.ring")
             : (context.selection_toggle ? "operation.hints.selection.toggle_loop" : "operation.hints.selection.loop"),
        ring ? (context.selection_toggle ? "Toggle Ring" : "Ring")
             : (context.selection_toggle ? "Toggle Loop" : "Loop"), true, 95));
    return hints;
  }
  const auto operation = context.selection_operation;
  hints.push_back(gesture(Gesture::Information,
      operation == QuaderInputSelectionOperation::Add ? "operation.hints.selection.add" :
      operation == QuaderInputSelectionOperation::Remove ? "operation.hints.selection.remove" : "operation.hints.selection.replace_operation",
      operation == QuaderInputSelectionOperation::Add ? "Add" :
      operation == QuaderInputSelectionOperation::Remove ? "Remove" : "Replace", true, 95));
  if (context.selection_gesture == Selection::Box) {
    hints.push_back(gesture(Gesture::Information,
        context.selection_crossing ? "operation.hints.selection.crossing" : "operation.hints.selection.contained",
        context.selection_crossing ? "Crossing" : "Contained", true, 80));
  } else {
    hints.push_back(binding("viewport.cancel_active_tool", "operation.hints.cancel_lasso", "Cancel Lasso", 90));
  }
  return hints;
}

std::vector<QuaderOperationInputHint>
lasso_selection_input_hints(const QuaderOperationInputContext &context) {
  if (context.selection_gesture != QuaderInputSelectionGesture::None) {
    return selection_gesture_input_hints(context);
  }
  return {gesture(Gesture::Shift, "operation.hints.selection.add", "Add", context.shift),
          gesture(Gesture::Control, "operation.hints.selection.remove", "Remove", context.control)};
}

std::vector<QuaderOperationInputHint>
radial_array_input_hints(bool can_finalize, bool center_locked) {
  Hints hints;
  if (!center_locked) hints.push_back(gesture(Gesture::PrimaryClick, "operation.hints.radial_array.choose_center", "Choose Center"));
  if (can_finalize) hints.push_back(binding("viewport.radial_array.finalize", "operation.hints.apply", "Apply", 95));
  hints.push_back(binding("viewport.cancel_active_tool", "operation.hints.cancel_array", "Cancel Array", 90));
  hints.push_back(wheel_pair("viewport.radial_array.increase_copies", "viewport.radial_array.decrease_copies", "operation.hints.array.copies", "Copies"));
  hints.push_back(wheel_pair("viewport.radial_array.increase_repeats", "viewport.radial_array.decrease_repeats", "operation.hints.array.repeats", "Repeats"));
  hints.push_back(binding("viewport.radial_array.toggle_instances", "operation.hints.array.instances", "Instances"));
  return hints;
}

std::vector<QuaderOperationInputHint>
primitive_creation_input_hints(QuaderPrimitiveCreationShape kind, bool can_finalize,
                               const QuaderOperationInputContext &context) {
  using namespace quader_command;
  Hints hints;
  if (can_finalize && !context.pointer_dragging) {
    hints.push_back({Gesture::Command, kToolsApplyPrimitiveCreation, "operation.hints.apply", "Apply", 95});
  }
  hints.push_back({Gesture::Command, kToolsCancelPrimitiveCreation, "operation.hints.cancel_creation", "Cancel Creation", 90});
  grid_snap(hints, context);
  hints.push_back({Gesture::Command, kToolsFlipPrimitiveFaces, "operation.hints.flip_faces", "Flip Faces"});
  if (kind == QuaderPrimitiveCreationShape::Cylinder || kind == QuaderPrimitiveCreationShape::Cone) {
    hints.push_back({Gesture::Command, kToolsTogglePrimitiveCaps, "operation.hints.toggle_caps", "Toggle Caps"});
  } else if (kind == QuaderPrimitiveCreationShape::Sphere) {
    hints.push_back({Gesture::Command, kToolsTogglePrimitiveHemisphere, "operation.hints.toggle_hemisphere", "Toggle Hemisphere"});
  }
  if (context.primitive_control != QuaderInputPrimitiveControl::None && !context.pointer_dragging) {
    std::string_view key = "operation.hints.primitive.subdivisions";
    std::string_view label = "Subdivisions";
    switch (context.primitive_control) {
    case QuaderInputPrimitiveControl::SubdivisionsX: key = "operation.hints.primitive.subdivisions_x"; label = "X Subdivisions"; break;
    case QuaderInputPrimitiveControl::SubdivisionsY: key = "operation.hints.primitive.subdivisions_y"; label = "Y Subdivisions"; break;
    case QuaderInputPrimitiveControl::SubdivisionsZ: key = "operation.hints.primitive.subdivisions_z"; label = "Z Subdivisions"; break;
    case QuaderInputPrimitiveControl::Sides: key = "operation.hints.primitive.sides"; label = "Sides"; break;
    case QuaderInputPrimitiveControl::Segments: key = "operation.hints.primitive.segments"; label = "Segments"; break;
    case QuaderInputPrimitiveControl::Rings: key = "operation.hints.primitive.rings"; label = "Rings"; break;
    case QuaderInputPrimitiveControl::MajorSegments: key = "operation.hints.primitive.major_segments"; label = "Major Segments"; break;
    case QuaderInputPrimitiveControl::MinorSegments: key = "operation.hints.primitive.minor_segments"; label = "Minor Segments"; break;
    case QuaderInputPrimitiveControl::SubdivisionLevel:
    case QuaderInputPrimitiveControl::None: break;
    }
    hints.push_back(gesture(Gesture::Wheel, key, label, false, 85));
    hints.push_back(gesture(Gesture::ControlPrimaryClick, "operation.hints.primitive.decrease", "Decrease", context.control, 80));
  }
  return hints;
}

std::vector<QuaderOperationInputHint>
viewport_tool_input_hints(std::string_view tool_id, const ModelingOperationHintState &state,
                          const QuaderOperationInputContext &context) {
  Hints hints;
  if (tool_id == "lasso_select") return lasso_selection_input_hints(context);
  if (tool_id == "select") return viewport_navigation_input_hints(context);
  if (tool_id == "create_point_light" || tool_id == "create_directional_light") {
    hints.push_back(binding("viewport.cancel_active_tool", "operation.hints.cancel_creation", "Cancel Creation", 90));
  } else if (tool_id == "uv_peel") {
    completion(hints, state.can_apply, "operation.hints.cancel_peel", "Cancel UV Peel");
  } else if (tool_id == "extrude") {
    completion(hints, state.extrusion_pending && !state.dragging,
        state.extrusion_pending ? "operation.hints.cancel_extrusion" : "operation.hints.exit_extrude",
        state.extrusion_pending ? "Cancel Extrusion" : "Exit Extrude");
    if (state.dragging) {
      // Extrusion owns this modifier directly; it is not the global snap toggle.
      hints.push_back(gesture(Gesture::Control, "operation.hints.extrude.grid_snap", "Grid Snap", context.control, 80));
      hints.push_back(gesture(Gesture::Shift, "operation.hints.precision", "Precision", context.shift, 80));
      auto vertex = binding("viewport.transform.vertex_snap", "operation.hints.vertex_snap", "Vertex Snap", 80);
      vertex.active = context.held_vertex_snap;
      hints.push_back(vertex);
      if (state.extrusion_region) {
        auto axes = binding("viewport.extrude.constraint_x", "operation.hints.axis_constraint", "Axis Constraint", 70);
        axes.related_commands = {QuaderCommandId{"viewport.extrude.constraint_y"}, QuaderCommandId{"viewport.extrude.constraint_z"}, {}};
        axes.related_command_count = 2;
        hints.push_back(axes);
      }
    } else {
      hints.push_back({Gesture::Command, quader_command::kToolsActivateExtrudeIndividual,
                       "operation.hints.extrude.individual", "Individual"});
      hints.push_back({Gesture::Command, quader_command::kToolsActivateExtrudeAlongNormals,
                       "operation.hints.extrude.normals", "Along Normals"});
    }
  } else if (tool_id == "insert_edge_loop") {
    grid_snap(hints, context);
    hints.push_back(binding("viewport.cancel_active_tool", "operation.hints.exit_tool", "Exit Tool", 90));
  } else if (tool_id == "knife" || tool_id == "poly") {
    completion(hints, state.can_apply && !state.dragging, "operation.hints.cancel", "Cancel");
    if (!state.closed) {
      grid_snap(hints, context);
      if (state.active_stroke) {
        hints.push_back(gesture(Gesture::Shift,
            tool_id == "knife" ? "operation.hints.knife.straight" : "operation.hints.constrain_axis",
            tool_id == "knife" ? "Straight Cut" : "Constrain Axis", context.shift, 80));
      }
      if (context.stroke_close_hovered) {
        hints.push_back(gesture(Gesture::PrimaryClick,
            tool_id == "knife" ? "operation.hints.knife.close" : "operation.hints.poly.close",
            tool_id == "knife" ? "Close Stroke" : "Close Polygon", false, 80));
      }
    }
  } else if (tool_id == "cut" || tool_id == "mirror") {
    completion(hints, state.can_apply && !state.dragging, "operation.hints.cancel", "Cancel");
    if (context.plane_endpoint_hovered) hints.push_back(gesture(Gesture::PrimaryDrag, "operation.hints.plane.endpoint", "Drag Endpoint", false, 80));
    else if (context.plane_body_hovered) hints.push_back(gesture(Gesture::PrimaryDrag, "operation.hints.plane.body", "Drag Plane", false, 80));
    if (tool_id == "cut") hints.push_back(binding("viewport.modal_tool.mode_cycle", "operation.hints.cut.keep_mode", "Cycle Keep Mode"));
  } else if (state.can_apply) {
    hints.push_back({Gesture::Command, quader_command::kModelingApplyActivePreview,
                     "operation.hints.apply", "Apply", 95});
    hints.push_back({Gesture::Command, quader_command::kModelingCancelActivePreview,
                     "operation.hints.cancel", "Cancel", 90});
  }
  return hints;
}

std::vector<QuaderOperationInputHint>
viewport_navigation_input_hints(const QuaderOperationInputContext &context) {
  if (!context.viewport_available) return {};
  switch (context.navigation) {
  case QuaderInputNavigation::Fly: {
    if (context.orthographic) return {};
    auto move = binding("viewport.navigation.fly_forward", "operation.hints.navigation.move", "Move", 90);
    move.related_commands = {QuaderCommandId{"viewport.navigation.fly_left"}, QuaderCommandId{"viewport.navigation.fly_backward"}, QuaderCommandId{"viewport.navigation.fly_right"}};
    move.related_command_count = 3;
    return {move,
      binding("viewport.navigation.zoom", "operation.hints.navigation.fly_speed", "Fly Speed", 90),
      binding("viewport.navigation.reset_fly_speed", "operation.hints.navigation.reset_fly_speed", "Reset Fly Speed", 80)};
  }
  case QuaderInputNavigation::Orbit: {
    if (!context.orbit_available) return {};
    auto hint = binding("viewport.navigation.orbit_snap", "operation.hints.navigation.snap_view", "Snap View", 80);
    hint.gesture = Gesture::KeybindingModifier;
    hint.active = context.orbit_snap_active;
    return {hint};
  }
  case QuaderInputNavigation::Pan: return {};
  case QuaderInputNavigation::Zoom: {
    auto hint = binding("viewport.navigation.zoom_to_cursor", "operation.hints.navigation.zoom_cursor", "Zoom at Cursor", 80);
    hint.gesture = Gesture::KeybindingModifier;
    hint.active = context.control;
    return {hint};
  }
  case QuaderInputNavigation::None: break;
  }
  Hints hints;
  if (context.orbit_available) hints.push_back(binding("viewport.navigation.orbit", "operation.hints.navigation.orbit", "Orbit"));
  hints.push_back(binding("viewport.navigation.pan", "operation.hints.navigation.pan", "Pan"));
  hints.push_back(binding("viewport.navigation.fly",
      context.orthographic ? "operation.hints.navigation.pan" : "operation.hints.navigation.fly",
      context.orthographic ? "Pan" : "Fly Navigation"));
  return hints;
}

std::vector<QuaderOperationInputHint>
viewport_auxiliary_input_hints(const QuaderOperationInputContext &context) {
  if (context.point_light_range_dragging) {
    return {binding("viewport.cancel_active_tool", "operation.hints.cancel_light_range", "Cancel Light Range", 90)};
  }
  if (context.point_light_range_hovered) {
    return {gesture(Gesture::PrimaryDrag, "operation.hints.light_range", "Adjust Light Range", false, 80)};
  }
  switch (context.material_transfer) {
  case QuaderInputMaterialTransfer::SelectedFaceToFace:
    return {binding("viewport.material.apply_selected_face_to_face", "operation.hints.material.selected_to_face", "Material to Face", 85)};
  case QuaderInputMaterialTransfer::SelectedFaceToMesh:
    return {binding("viewport.material.apply_selected_face_to_mesh", "operation.hints.material.selected_to_mesh", "Material to Mesh", 85)};
  case QuaderInputMaterialTransfer::FaceToSelection:
    return {binding("viewport.material.apply_face_to_selection", "operation.hints.material.face_to_selection", "Material to Selection", 85)};
  case QuaderInputMaterialTransfer::FaceToMesh:
    return {binding("viewport.material.apply_face_to_mesh", "operation.hints.material.face_to_mesh", "Material Across Mesh", 85)};
  case QuaderInputMaterialTransfer::ContinueTexture:
    return {binding("viewport.material.transfer_face_texture", "operation.hints.material.continue_texture", "Continue Texture", 85)};
  case QuaderInputMaterialTransfer::None: return {};
  }
  return {};
}

} // namespace quader

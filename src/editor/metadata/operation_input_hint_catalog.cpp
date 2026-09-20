////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/operation_input_hint_catalog.hpp"

#include <tuple>

namespace quader {
namespace {

using Gesture = QuaderOperationInputGesture;

QuaderOperationInputHint gesture_hint(Gesture gesture,
                                      std::string_view key,
                                      std::string_view fallback) {
  return {gesture, {}, key, fallback};
}

QuaderOperationInputHint keybinding_hint(std::string_view target_id,
                                         std::string_view key,
                                         std::string_view fallback) {
  return {Gesture::Keybinding, QuaderCommandId{target_id}, key, fallback};
}

void append_selection_click_hints(std::vector<QuaderOperationInputHint> &hints) {
  hints.push_back(gesture_hint(Gesture::PrimaryClick,
                               "operation.hints.selection.replace", "Select"));
  hints.push_back(gesture_hint(Gesture::ShiftPrimaryClick,
                               "operation.hints.selection.add", "Add"));
  hints.push_back(gesture_hint(Gesture::ControlPrimaryClick,
                               "operation.hints.selection.remove", "Remove"));
}

void append_selection_keybinding_hints(
    std::vector<QuaderOperationInputHint> &hints) {
  hints.push_back(keybinding_hint(
      "viewport.frame_selection",
      "keybindings.target.viewport.frame_selection.label", "Focus Selection"));

}

void append_modal_completion_hints(
    std::vector<QuaderOperationInputHint> &hints) {
  hints.push_back(keybinding_hint("viewport.modal_tool.finalize",
                                  "operation.hints.finalize", "Finalize"));
  hints.push_back(keybinding_hint("viewport.cancel_active_tool",
                                  "operation.hints.cancel", "Cancel"));
}

} // namespace

std::vector<QuaderOperationInputHint> lasso_selection_input_hints() {
  return {
      {Gesture::PrimaryDrag, {}, "operation.hints.lasso.replace", "Select"},
      {Gesture::ShiftPrimaryDrag, {}, "operation.hints.lasso.add", "Add"},
      {Gesture::ControlPrimaryDrag, {}, "operation.hints.lasso.remove", "Remove"},
  };
}

std::vector<QuaderOperationInputHint>
radial_array_input_hints(bool can_finalize, bool center_locked) {
  using Gesture = QuaderOperationInputGesture;
  std::vector<QuaderOperationInputHint> hints;
  hints.reserve(8);
  if (!center_locked) {
    hints.push_back({Gesture::PrimaryClick, {},
                     "operation.hints.radial_array.choose_center", "Choose center"});
  }
  if (can_finalize) {
    hints.push_back({Gesture::Keybinding,
                     QuaderCommandId{"viewport.radial_array.finalize"},
                     "operation.hints.finalize", "Finalize"});
  }
  hints.push_back({Gesture::Keybinding,
                   QuaderCommandId{"viewport.cancel_active_tool"},
                   "operation.hints.cancel", "Cancel"});
  hints.push_back({Gesture::Keybinding,
                   QuaderCommandId{"viewport.radial_array.increase_copies"},
                   "operation.hints.radial_array.increase_copies", "More copies"});
  hints.push_back({Gesture::Keybinding,
                   QuaderCommandId{"viewport.radial_array.decrease_copies"},
                   "operation.hints.radial_array.decrease_copies", "Fewer copies"});
  hints.push_back({Gesture::Keybinding,
                   QuaderCommandId{"viewport.radial_array.increase_repeats"},
                   "operation.hints.radial_array.increase_repeats", "More repeats"});
  hints.push_back({Gesture::Keybinding,
                   QuaderCommandId{"viewport.radial_array.decrease_repeats"},
                   "operation.hints.radial_array.decrease_repeats", "Fewer repeats"});
  hints.push_back({Gesture::Keybinding,
                   QuaderCommandId{"viewport.radial_array.toggle_instances"},
                   "operation.hints.radial_array.toggle_instances", "Toggle instances"});
  return hints;
}

std::vector<QuaderOperationInputHint>
primitive_creation_input_hints(QuaderPrimitiveCreationShape kind, bool can_finalize) {
  using namespace quader_command;
  std::vector<QuaderOperationInputHint> hints;
  hints.reserve(5);
  hints.push_back({Gesture::PrimaryDrag, {},
                   "operation.hints.primitive.draw", "Draw / resize"});
  if (can_finalize) {
    hints.push_back({Gesture::Command, kToolsApplyPrimitiveCreation,
                     "operation.hints.finalize", "Finalize"});
  }
  hints.push_back({Gesture::Command, kToolsCancelPrimitiveCreation,
                   "operation.hints.cancel", "Cancel"});
  hints.push_back({Gesture::Command, kToolsFlipPrimitiveFaces,
                   "operation.hints.flip", "Flip"});
  if (kind == QuaderPrimitiveCreationShape::Cylinder || kind == QuaderPrimitiveCreationShape::Cone) {
    hints.push_back({Gesture::Command, kToolsTogglePrimitiveCaps,
                     "operation.hints.caps", "Caps"});
  } else if (kind == QuaderPrimitiveCreationShape::Sphere) {
    hints.push_back({Gesture::Command, kToolsTogglePrimitiveHemisphere,
                     "operation.hints.hemisphere", "Hemisphere"});
  }
  return hints;
}

std::vector<QuaderOperationInputHint>
viewport_tool_input_hints(std::string_view tool_id) {
  std::vector<QuaderOperationInputHint> hints;
  hints.reserve(12);

  if (tool_id == "lasso_select") {
    hints = lasso_selection_input_hints();
    append_selection_keybinding_hints(hints);
  } else if (tool_id == "select") {
    append_selection_click_hints(hints);
    append_selection_keybinding_hints(hints);
  } else if (tool_id == "uv_peel") {
    append_selection_click_hints(hints);
    append_modal_completion_hints(hints);
  } else if (tool_id == "extrude") {
    hints.push_back({Gesture::Command, quader_command::kToolsActivateExtrude,
                     "operation.hints.tool.extrude", "Extrude"});
    hints.push_back({Gesture::Command, quader_command::kToolsActivateExtrudeIndividual,
                     "extrude.type.individual", "Extrude individually"});
    hints.push_back({Gesture::Command, quader_command::kToolsActivateExtrudeAlongNormals,
                     "extrude.type.along_normals", "Extrude along normals"});
    hints.push_back(gesture_hint(Gesture::PrimaryDrag,
                                 "operation.hints.extrude.handles",
                                 "Adjust extrusion"));
    hints.push_back(gesture_hint(Gesture::ControlPrimaryDrag,
                                 "operation.hints.extrude.snap", "Snap to grid"));
    hints.push_back(gesture_hint(Gesture::ShiftPrimaryDrag,
                                 "operation.hints.extrude.precision", "Precision"));
    hints.push_back(keybinding_hint("viewport.transform.vertex_snap",
                                    "keybindings.target.viewport.transform.vertex_snap.label",
                                    "Hold Vertex Snap"));
    append_selection_click_hints(hints);
    for (const auto &[target, key, fallback] : {
             std::tuple{"viewport.extrude.constraint_x",
                        "keybindings.target.viewport.extrude.constraint_x.label",
                        "Constrain X"},
             std::tuple{"viewport.extrude.constraint_y",
                        "keybindings.target.viewport.extrude.constraint_y.label",
                        "Constrain Y"},
             std::tuple{"viewport.extrude.constraint_z",
                        "keybindings.target.viewport.extrude.constraint_z.label",
                        "Constrain Z"}}) {
      hints.push_back(keybinding_hint(target, key, fallback));
    }
    hints.push_back(keybinding_hint("viewport.cancel_active_tool",
                                    "operation.hints.cancel", "Cancel"));
    append_selection_keybinding_hints(hints);
  } else if (tool_id == "move" || tool_id == "rotate" ||
             tool_id == "scale") {
    const std::string_view key =
        tool_id == "move"       ? "operation.hints.tool.move"
        : tool_id == "rotate"   ? "operation.hints.tool.rotate"
        : tool_id == "scale"    ? "operation.hints.tool.scale"
                                  : "operation.hints.tool.extrude";
    const std::string_view fallback =
        tool_id == "move"       ? "Move"
        : tool_id == "rotate"   ? "Rotate"
        : tool_id == "scale"    ? "Scale"
                                  : "Extrude";
    hints.push_back(gesture_hint(Gesture::PrimaryDrag, key, fallback));
    if (tool_id == "move") {
      hints.push_back(keybinding_hint(
          "viewport.transform.vertex_snap",
          "keybindings.target.viewport.transform.vertex_snap.label",
          "Hold Vertex Snap"));
    }
    append_selection_click_hints(hints);
    append_selection_keybinding_hints(hints);
  } else if (tool_id == "insert_edge_loop") {
    hints.push_back(gesture_hint(Gesture::PrimaryDrag,
                                 "operation.hints.tool.edge_loop",
                                 "Insert / slide edge loop"));
    hints.push_back(keybinding_hint("viewport.cancel_active_tool",
                                    "operation.hints.cancel", "Cancel"));
  } else if (tool_id == "knife" || tool_id == "poly" || tool_id == "cut" ||
             tool_id == "mirror") {
    const std::string_view key =
        tool_id == "knife"    ? "operation.hints.tool.knife"
        : tool_id == "poly"   ? "operation.hints.tool.poly"
        : tool_id == "cut"    ? "operation.hints.tool.cut"
                                : "operation.hints.tool.mirror";
    const std::string_view fallback =
        tool_id == "knife"    ? "Place cut point"
        : tool_id == "poly"   ? "Place polygon point"
        : tool_id == "cut"    ? "Define cut"
                                : "Define mirror plane";
    hints.push_back(gesture_hint(Gesture::PrimaryClick, key, fallback));
    append_modal_completion_hints(hints);
    if (tool_id == "cut") {
      hints.push_back(keybinding_hint(
          "viewport.modal_tool.mode_cycle",
          "keybindings.target.viewport.modal_tool.mode_cycle.label",
          "Cycle Cut Keep Mode"));
    }
  } else if (tool_id == "pivot") {
    hints.push_back(gesture_hint(Gesture::PrimaryDrag,
                                 "operation.hints.tool.pivot", "Move pivot"));
    append_modal_completion_hints(hints);
  } else if (tool_id == "linear_array") {
    hints.push_back(gesture_hint(Gesture::PrimaryDrag,
                                 "operation.hints.tool.linear_array",
                                 "Adjust array endpoint"));
    append_modal_completion_hints(hints);
    for (const auto &[target, key, fallback] : {
             std::tuple{"viewport.linear_array.constraint_x",
                        "keybindings.target.viewport.linear_array.constraint_x.label",
                        "Constrain X"},
             std::tuple{"viewport.linear_array.constraint_y",
                        "keybindings.target.viewport.linear_array.constraint_y.label",
                        "Constrain Y"},
             std::tuple{"viewport.linear_array.constraint_z",
                        "keybindings.target.viewport.linear_array.constraint_z.label",
                        "Constrain Z"},
             std::tuple{"viewport.linear_array.increase_copies",
                        "keybindings.target.viewport.linear_array.increase_copies.label",
                        "More copies"},
             std::tuple{"viewport.linear_array.decrease_copies",
                        "keybindings.target.viewport.linear_array.decrease_copies.label",
                        "Fewer copies"}}) {
      hints.push_back(keybinding_hint(target, key, fallback));
    }
  } else if (tool_id == "path_array") {
    hints.push_back(gesture_hint(Gesture::PrimaryClick,
                                 "operation.hints.tool.path_array",
                                 "Choose source"));
    append_modal_completion_hints(hints);
  } else if (tool_id == "curve") {
    hints.push_back(gesture_hint(Gesture::PrimaryClick,
                                 "operation.hints.tool.curve",
                                 "Place / select point"));
    hints.push_back(keybinding_hint(
        "viewport.curve.toggle_point_smoothness",
        "keybindings.target.viewport.curve.toggle_point_smoothness.label",
        "Toggle Corner / Smooth"));
    hints.push_back(keybinding_hint(
        "viewport.transform.vertex_snap",
        "keybindings.target.viewport.transform.vertex_snap.label",
        "Hold Vertex Snap"));
    append_modal_completion_hints(hints);
  }
  return hints;
}

std::vector<QuaderOperationInputHint> viewport_navigation_input_hints() {
  return {
      keybinding_hint("viewport.navigation.orbit",
                      "keybindings.target.viewport.navigation.orbit.label",
                      "Orbit View"),
      keybinding_hint("viewport.navigation.pan",
                      "keybindings.target.viewport.navigation.pan.label",
                      "Pan View"),
      keybinding_hint("viewport.navigation.orbit_snap",
                      "keybindings.target.viewport.navigation.orbit_snap.label",
                      "Orbit View with Snap"),
      keybinding_hint("viewport.navigation.fly",
                      "keybindings.target.viewport.navigation.fly.label",
                      "Fly / Orthographic Pan"),
      keybinding_hint("viewport.navigation.zoom",
                      "keybindings.target.viewport.navigation.zoom.label",
                      "Zoom / Adjust Fly Speed"),
  };
}

} // namespace quader

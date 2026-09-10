////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/operation_input_hint_catalog.hpp"

namespace quader {

std::vector<QuaderOperationInputHint> lasso_selection_input_hints() {
  using Gesture = QuaderOperationInputGesture;
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
  using Gesture = QuaderOperationInputGesture;
  using namespace quader_command;
  std::vector<QuaderOperationInputHint> hints;
  hints.reserve(4);
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

} // namespace quader

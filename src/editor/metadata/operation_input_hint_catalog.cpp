////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/metadata/operation_input_hint_catalog.hpp"

namespace quader {

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

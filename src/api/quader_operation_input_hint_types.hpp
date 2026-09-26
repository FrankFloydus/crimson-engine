////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "api/quader_operation_input_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace quader {

/// A command, canonical binding, or intrinsic gesture owned by a local control.
enum class QuaderOperationInputGesture {
  Command, PrimaryClick, ShiftPrimaryClick, ControlPrimaryClick, Wheel,
  PrimaryDrag, ShiftPrimaryDrag, ControlPrimaryDrag, Keybinding, KeybindingHold, KeybindingModifier,
  Shift, Control, Alt, ControlShift, MiddleClick, RightClick, Escape,
  Enter, EscapeOrRightClick, MiddleDrag, ShiftMiddleDrag, ControlMiddleDrag,
  MiddleDoubleClick, PrimaryDoubleClick, ControlWheel, ShiftWheel,
  PrimaryOrMiddleDrag, PrimaryOrMiddleDoubleClick, ShiftAltPrimaryDrag, Information,
};

/// Presentation only; input owners remain authoritative for dispatch.
struct QuaderOperationInputHint {
  QuaderOperationInputGesture gesture = QuaderOperationInputGesture::Command;
  QuaderCommandId command;
  std::string_view label_key;
  std::string_view label_fallback;
  /// Higher priorities survive narrow status bars before optional alternatives.
  std::uint8_t priority = 50;
  bool active = false;
  /// Related canonical bindings (axes, movement, wheel pairs), resolved together.
  /// The command field is the first binding; these are additional bindings.
  std::array<QuaderCommandId, 3> related_commands = {};
  std::uint8_t related_command_count = 0;
  /// Optional owned target name for hierarchy and folder drop destinations.
  std::string detail;
};

} // namespace quader

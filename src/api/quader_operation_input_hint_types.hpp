////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"

#include <string_view>

namespace quader {

/// A command binding or an intrinsic gesture owned by a tool/operation.
enum class QuaderOperationInputGesture {
  Command,
  PrimaryClick,
  Wheel,
  PrimaryDrag,
  ShiftPrimaryDrag,
  ControlPrimaryDrag,
};

/// Presentation metadata only; commands and tool owners still resolve input.
struct QuaderOperationInputHint {
  QuaderOperationInputGesture gesture = QuaderOperationInputGesture::Command;
  QuaderCommandId command;
  std::string_view label_key;
  std::string_view label_fallback;
};

} // namespace quader

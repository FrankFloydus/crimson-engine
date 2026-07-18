////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "editor/modeling/modeling_types.hpp"

#include <utility>

namespace quader {

/**
 * Projects native modeling outcomes into editor-history command outcomes. The
 * two contracts deliberately share only ok/changed/message.
 */
[[nodiscard]] inline CommandResult
command_result_from_modeling(ModelingCommandResult result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = std::move(result.message),
  };
}

[[nodiscard]] inline CommandResult
command_result_from_modeling(const ModelingCreateBoxResult &result) {
  return {
      .ok = result.ok,
      .changed = result.changed,
      .message = result.message,
  };
}

} // namespace quader

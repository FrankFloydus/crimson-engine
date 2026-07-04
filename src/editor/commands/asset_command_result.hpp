////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"

#include <string>
#include <utility>

namespace quader {

using AssetCommandResult = CommandResult;

/**
 * Stores Add Asset Command payload data for asset command callers.
 */
struct AssetCreateResult : AssetCommandResult {
  AssetId asset = kInvalidAssetId;
};

inline AssetCommandResult asset_command_changed_result() {
  return command_changed_result();
}

inline AssetCommandResult
asset_command_unchanged_result(std::string message = {}) {
  return {
      .ok = true,
      .changed = false,
      .message = std::move(message),
  };
}

inline AssetCommandResult
asset_command_invalid_request_result(std::string message) {
  return command_invalid_result(std::move(message));
}

} // namespace quader

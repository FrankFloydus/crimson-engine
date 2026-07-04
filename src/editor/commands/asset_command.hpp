////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/i_undoable_command.hpp"
#include "editor/commands/asset_command_result.hpp"

#include <string>
#include <utility>

namespace quader {

class AssetRegistry;

/**
 * Applies Asset Command behavior as a command in the editor asset command layer.
 */
class AssetCommand : public IUndoableCommand {
public:
  [[nodiscard]] const std::string &label() const override { return label_; }

protected:
  AssetCommand(AssetRegistry &assets, std::string label)
      : assets_(assets), label_(std::move(label)) {}

  [[nodiscard]] AssetRegistry &assets() { return assets_; }
  [[nodiscard]] const AssetRegistry &assets() const { return assets_; }

private:
  AssetRegistry &assets_;
  std::string label_;
};

} // namespace quader

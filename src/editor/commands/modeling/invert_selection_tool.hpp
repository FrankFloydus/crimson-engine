////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/custom_instant_command_tool.hpp"

namespace quader {

/**
 * Implements Invert Selection Tool behavior for the editor command and action routing layer.
 */
class InvertSelectionTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
      return {
          .id = "invert_selection",
          .operation_id = "invert_selection",
          .label = "Invert Selection",
          .kind = CommandToolKind::Instant,
          .preview_kind = ModelingActionPreviewKind::None,
          .rebuilds_mesh_on_success = false,
          .public_command_id = "edit.invert_selection",
          .registers_public_command = false,
      };
    }

private:
  ModelingCommandResult execute_custom_command(
      ModelingCommandExecutionContext &context) const override;
};

} // namespace quader

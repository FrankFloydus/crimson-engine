////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/runtime_command_tool.hpp"

namespace quader {

/**
 * Implements Flip Horizontal Tool behavior for the editor command and action routing layer.
 */
class FlipHorizontalTool final : public RuntimeCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
      return {
          .id = "flip_horizontal",
          .operation_id = "flip_horizontal",
          .label = "Flip Horizontal",
          .kind = CommandToolKind::Instant,
          .preview_kind = ModelingActionPreviewKind::None,
          .rebuilds_mesh_on_success = true,
      };
    }

private:
  ModelingCommandResult execute_runtime_command(
      ModelingCommandExecutionContext &context) const override;
};

} // namespace quader

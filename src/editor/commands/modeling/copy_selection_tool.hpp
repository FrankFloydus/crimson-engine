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
 * Implements Copy Selection Tool behavior for the editor command and action routing layer.
 */
class CopySelectionTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
      return {
          .id = "copy_selection",
          .operation_id = "copy_selection",
          .label = "Copy",
          .kind = CommandToolKind::Instant,
          .preview_kind = ModelingActionPreviewKind::None,
          .rebuilds_mesh_on_success = false,
          .public_command_id = "edit.copy",
          .registers_public_command = false,
          .host_shortcut_target_id = "copy_meshes",
      };
    }

private:
  ModelingCommandResult execute_custom_command(
      ModelingCommandExecutionContext &context) const override;
};

} // namespace quader

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
 * Implements Delete Selection Tool behavior for component-mode editor command routing.
 */
class DeleteSelectionTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
      return {
          .id = "delete_selection",
          .operation_id = "delete_selection",
          .label = "Delete",
          .kind = CommandToolKind::Instant,
          .preview_kind = ModelingActionPreviewKind::None,
          .rebuilds_mesh_on_success = true,
          .public_command_id = "edit.delete",
          .registers_public_command = false,
      };
    }

private:
  [[nodiscard]] ModelingCommandAvailability custom_availability(
      const ModelingCommandExecutionContext &context) const override;
  ModelingCommandResult execute_custom_command(
      ModelingCommandExecutionContext &context) const override;
};

} // namespace quader

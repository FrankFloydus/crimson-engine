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
 * Implements Paste Selection Tool behavior for the editor command and action routing layer.
 */
class PasteSelectionTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
      return {
          .id = "paste_selection",
          .operation_id = "paste_selection",
          .label = "Paste",
          .kind = CommandToolKind::Instant,
          .preview_kind = ModelingActionPreviewKind::None,
          .rebuilds_mesh_on_success = true,
          .public_command_id = "edit.paste",
          .registers_public_command = false,
          .host_shortcut_target_id = "paste_meshes",
      };
    }

private:
  [[nodiscard]] ModelingCommandAvailability custom_availability(
      const ModelingCommandExecutionContext &context) const override;
  ModelingCommandResult execute_custom_command(
      ModelingCommandExecutionContext &context) const override;
};

} // namespace quader

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
                .operationId = "delete_selection",
                .label = "Delete",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
                .publicCommandId = "edit.delete",
                .registersPublicCommand = false,
        };
    }

private:
    [[nodiscard]] ModelingCommandAvailability customAvailability(
            const ModelingCommandExecutionContext& context) const override;
    ModelingCommandResult executeCustomCommand(ModelingCommandExecutionContext& context) const override;
};

} // namespace quader

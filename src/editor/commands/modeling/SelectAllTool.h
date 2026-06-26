////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/CustomInstantCommandTool.h"

namespace quader {

/**
 * Implements Select All Tool behavior for the editor command and action routing layer.
 */
class SelectAllTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "select_all",
                .operationId = "select_all",
                .label = "Select All",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = false,
                .publicCommandId = "edit.select_all",
                .registersPublicCommand = false,
        };
    }

private:
    ModelingCommandResult executeCustomCommand(ModelingCommandExecutionContext& context) const override;
};

} // namespace quader

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
 * Implements Copy Selection Tool behavior for the editor command and action routing layer.
 */
class CopySelectionTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "copy_selection",
                .operationId = "copy_selection",
                .label = "Copy",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = false,
        };
    }

private:
    ModelingCommandResult executeCustomCommand(ModelingCommandExecutionContext& context) const override;
};

} // namespace quader

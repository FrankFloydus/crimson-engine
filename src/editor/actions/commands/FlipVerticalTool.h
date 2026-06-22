////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/actions/RuntimeCommandTool.h"

namespace quader {

/**
 * Implements Flip Vertical Tool behavior for the editor command and action routing layer.
 */
class FlipVerticalTool final : public RuntimeCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "flip_vertical",
                .operationId = "flip_vertical",
                .label = "Flip Vertical",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

private:
    ModelingCommandResult executeRuntimeCommand(ModelingCommandExecutionContext& context) const override;
};

} // namespace quader

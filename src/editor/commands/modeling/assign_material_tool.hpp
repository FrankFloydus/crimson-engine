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
 * Implements Assign Material Tool behavior for the editor command and action routing layer.
 */
class AssignMaterialTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "assign_material",
                .operationId = "assign_material",
                .label = "Assign Material",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
                .publicCommandId = "edit.apply_material",
                .registersPublicCommand = false,
        };
    }

private:
    ModelingCommandResult executeCustomCommand(ModelingCommandExecutionContext& context) const override;
};

} // namespace quader

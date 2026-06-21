#pragma once

#include "modeling/tools/RuntimeCommandTool.h"

namespace quader {

class FlipHorizontalTool final : public RuntimeCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "flip_horizontal",
                .operationId = "flip_horizontal",
                .label = "Flip Horizontal",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

private:
    ModelingCommandResult executeRuntimeCommand(ModelingCommandExecutionContext& context) const override;
};

} // namespace quader

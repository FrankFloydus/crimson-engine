#pragma once

#include "modeling/tools/RuntimeCommandTool.h"

namespace quader {

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

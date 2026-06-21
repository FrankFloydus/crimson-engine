#pragma once

#include "modeling/tools/CustomInstantCommandTool.h"

namespace quader {

class RepeatLastActionTool final : public CustomInstantCommandTool {
public:
    [[nodiscard]] CommandToolDescriptor descriptor() const override {
        return {
                .id = "repeat_last_action",
                .operationId = "repeat_last_action",
                .label = "Repeat Last Action",
                .kind = CommandToolKind::Instant,
                .previewKind = ModelingActionPreviewKind::None,
                .rebuildsMeshOnSuccess = true,
        };
    }

private:
    ModelingCommandResult executeCustomCommand(ModelingCommandExecutionContext& context) const override;
};

} // namespace quader

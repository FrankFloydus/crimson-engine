#pragma once

#include "modeling/tools/ContextActionTool.h"

namespace quader {

class SdkContextOperationTool : public ContextActionTool {
public:
    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const final {
        const std::optional<ModelingActionItem> live = actionItem(context);
        if (!live.has_value()) {
            return ModelingCommandAvailability::unavailable("Command is not available in the current mode.");
        }
        if (live->visibility == ModelingActionVisibility::Enabled) {
            return ModelingCommandAvailability::available();
        }
        return ModelingCommandAvailability::unavailable(
                live->disabledReason.empty() ? "Command is disabled for the current selection." : live->disabledReason);
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
        return context.modeling.executeContextAction(actionId());
    }
};

} // namespace quader

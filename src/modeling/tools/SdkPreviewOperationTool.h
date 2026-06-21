#pragma once

#include "modeling/tools/PreviewCommandTool.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace quader {

class SdkPreviewOperationTool : public PreviewCommandTool {
public:
    [[nodiscard]] std::optional<ModelingActionItem> actionItem(
            const ModelingCommandExecutionContext& context,
            ModelingActionQuery query = defaultActionQuery()) const override {
        const std::vector<ModelingActionItem> actions = context.modeling.contextActions(query);
        const auto found = std::find_if(actions.begin(), actions.end(), [&](const ModelingActionItem& item) {
            return item.actionId == actionId();
        });
        if (found == actions.end()) {
            return std::nullopt;
        }
        return *found;
    }

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

    ModelingCommandResult beginPreview(ModelingCommandExecutionContext& context) const final {
        return context.modeling.executeContextAction(actionId());
    }
};

} // namespace quader

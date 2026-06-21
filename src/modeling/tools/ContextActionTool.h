#pragma once

#include "modeling/ModelingSessionService.h"
#include "modeling/tools/CommandTool.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace quader {

class ContextActionTool : public CommandTool {
public:
    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy() const override {
        return rebuildsMeshOnSuccess() ? ModelingCommandHistoryPolicy::modelingUndoRedo() :
                                         ModelingCommandHistoryPolicy::none();
    }

    [[nodiscard]] virtual std::string_view actionId() const = 0;

    [[nodiscard]] std::optional<ModelingActionItem> actionItem(
            const ModelingCommandExecutionContext& context,
            ModelingActionQuery query) const {
        const std::vector<ModelingActionItem> actions = context.modeling.contextActions(query);
        const auto found = std::find_if(actions.begin(), actions.end(), [&](const ModelingActionItem& item) {
            return item.actionId == actionId();
        });
        if (found == actions.end()) {
            return std::nullopt;
        }
        return *found;
    }

    [[nodiscard]] std::optional<ModelingActionItem> actionItem(
            const ModelingCommandExecutionContext& context) const {
        return actionItem(context, actionQuery());
    }

protected:
    [[nodiscard]] virtual ModelingActionQuery actionQuery() const {
        return defaultActionQuery();
    }

    [[nodiscard]] static ModelingActionQuery defaultActionQuery() {
        return {
                .surface = ModelingActionSurface::OperationsPanel,
                .modalToolOwnsViewport = false,
                .includeDisabled = true,
                .includeHidden = false,
        };
    }
};

} // namespace quader

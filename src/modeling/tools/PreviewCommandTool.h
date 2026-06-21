#pragma once

#include "modeling/ModelingSessionService.h"
#include "modeling/tools/CommandTool.h"

#include <optional>
#include <string_view>

namespace quader {

class PreviewCommandTool : public CommandTool {
public:
    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy() const override {
        return ModelingCommandHistoryPolicy::none();
    }

    [[nodiscard]] virtual std::string_view actionId() const = 0;
    [[nodiscard]] virtual std::optional<ModelingActionItem> actionItem(
            const ModelingCommandExecutionContext& context,
            ModelingActionQuery query = defaultActionQuery()) const = 0;
    virtual ModelingCommandResult beginPreview(ModelingCommandExecutionContext& context) const = 0;

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
        return beginPreview(context);
    }

    ModelingCommandResult beginPreview(ModelingSessionService& modeling) const {
        ModelingCommandExecutionContext context{.modeling = modeling};
        return beginPreview(context);
    }

    ModelingCommandResult commitPreview(ModelingSessionService& modeling) const {
        return modeling.commitActiveOperationPreview();
    }

    ModelingCommandResult cancelPreview(ModelingSessionService& modeling) const {
        return modeling.cancelActiveOperationPreview();
    }

    ModelingCommandResult applySettings(ModelingSessionService& modeling, ModelingInteractionSettings settings) const {
        return modeling.setInteractionSettings(settings);
    }

protected:
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

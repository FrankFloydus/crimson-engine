#pragma once

#include "modeling/ModelingSessionService.h"
#include "modeling/tools/CommandTool.h"

#include <optional>

namespace quader {

class InstantCommandTool : public CommandTool {
public:
    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy() const override {
        return rebuildsMeshOnSuccess() ? ModelingCommandHistoryPolicy::modelingUndoRedo() :
                                         ModelingCommandHistoryPolicy::none();
    }

    [[nodiscard]] virtual std::optional<ModelingInstantOperationId> instantOperation() const {
        return std::nullopt;
    }

    [[nodiscard]] bool hasInstantOperation() const { return instantOperation().has_value(); }
    [[nodiscard]] ModelingInstantOperationId operation() const {
        return instantOperation().value_or(ModelingInstantOperationId::DeleteSelection);
    }

    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
        (void)context;
        return ModelingCommandAvailability::available();
    }
};

} // namespace quader

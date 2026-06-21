#pragma once

#include "modeling/tools/InstantCommandTool.h"

namespace quader {

class CustomInstantCommandTool : public InstantCommandTool {
public:
    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
        return customAvailability(context);
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
        return executeCustomCommand(context);
    }

protected:
    [[nodiscard]] virtual ModelingCommandAvailability customAvailability(const ModelingCommandExecutionContext& context) const {
        (void)context;
        return ModelingCommandAvailability::available();
    }

    virtual ModelingCommandResult executeCustomCommand(ModelingCommandExecutionContext& context) const = 0;
};

} // namespace quader

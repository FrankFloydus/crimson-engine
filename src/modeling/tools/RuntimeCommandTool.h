#pragma once

#include "modeling/tools/InstantCommandTool.h"

namespace quader {

class RuntimeCommandTool : public InstantCommandTool {
public:
    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
        return runtimeAvailability(context);
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
        return executeRuntimeCommand(context);
    }

protected:
    [[nodiscard]] virtual ModelingCommandAvailability runtimeAvailability(const ModelingCommandExecutionContext& context) const {
        (void)context;
        return ModelingCommandAvailability::available();
    }

    virtual ModelingCommandResult executeRuntimeCommand(ModelingCommandExecutionContext& context) const = 0;
};

} // namespace quader

#pragma once

#include "modeling/tools/InstantCommandTool.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace quader {

class SdkInstantOperationTool : public InstantCommandTool {
public:
    [[nodiscard]] virtual ModelingInstantOperationId sdkInstantOperation() const = 0;

    [[nodiscard]] std::optional<ModelingInstantOperationId> instantOperation() const final {
        return sdkInstantOperation();
    }

    [[nodiscard]] std::optional<ModelingInstantOperationDescriptor> liveDescriptor(
            const ModelingCommandExecutionContext& context) const {
        const std::vector<ModelingInstantOperationDescriptor> operations = context.modeling.instantOperations();
        const auto found = std::find_if(operations.begin(), operations.end(), [&](const ModelingInstantOperationDescriptor& item) {
            return item.operation == sdkInstantOperation();
        });
        if (found == operations.end()) {
            return std::nullopt;
        }
        return *found;
    }

    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const final {
        const std::optional<ModelingInstantOperationDescriptor> live = liveDescriptor(context);
        if (live.has_value() && live->enabled) {
            return ModelingCommandAvailability::available();
        }
        return ModelingCommandAvailability::unavailable("Command is disabled for the current selection.");
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
        return context.modeling.executeInstantOperation(sdkInstantOperation());
    }
};

} // namespace quader

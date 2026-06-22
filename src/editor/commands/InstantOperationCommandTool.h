////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/commands/InstantCommandTool.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace quader {

/**
 * Implements Instant Operation Command Tool behavior for the editor command and action routing layer.
 */
class InstantOperationCommandTool : public InstantCommandTool {
public:
    [[nodiscard]] virtual ModelingInstantOperationId instantOperationId() const = 0;

    [[nodiscard]] std::optional<ModelingInstantOperationId> instantOperation() const final {
        return instantOperationId();
    }

    [[nodiscard]] std::optional<ModelingInstantOperationDescriptor> liveDescriptor(
            const ModelingCommandExecutionContext& context) const {
        const std::vector<ModelingInstantOperationDescriptor> operations = context.modeling.instantOperations();
        const auto found = std::find_if(operations.begin(), operations.end(), [&](const ModelingInstantOperationDescriptor& item) {
            return item.operation == instantOperationId();
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
        return context.modeling.executeInstantOperation(instantOperationId());
    }
};

} // namespace quader

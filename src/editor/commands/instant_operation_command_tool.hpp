////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/commands/instant_command_tool.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace quader {

/**
 * Implements Instant Operation Command Tool behavior for the editor command and action routing layer.
 */
class InstantOperationCommandTool : public InstantCommandTool {
public:
  [[nodiscard]] virtual ModelingInstantOperationId
  instant_operation_id() const = 0;

  [[nodiscard]] std::optional<ModelingInstantOperationId>
  instant_operation() const final {
    return instant_operation_id();
  }

  [[nodiscard]] std::optional<ModelingInstantOperationDescriptor>
  live_descriptor(const ModelingCommandExecutionContext &context) const {
    const std::vector<ModelingInstantOperationDescriptor> operations =
        context.modeling.instant_operations();
    const auto found =
        std::find_if(operations.begin(), operations.end(),
                     [&](const ModelingInstantOperationDescriptor &item) {
                       return item.operation == instant_operation_id();
                     });
    if (found == operations.end()) {
      return std::nullopt;
    }
    return *found;
  }

    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const final {
      const std::optional<ModelingInstantOperationDescriptor> live =
          live_descriptor(context);
      if (live.has_value() && live->enabled) {
        return ModelingCommandAvailability::available();
      }
        return ModelingCommandAvailability::unavailable("Command is disabled for the current selection.");
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
      return context.modeling.execute_instant_operation(instant_operation_id());
    }
};

} // namespace quader

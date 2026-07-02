////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/context_action_tool.hpp"

namespace quader {

/**
 * Implements Context Operation Command Tool behavior for the editor command and action routing layer.
 */
class ContextOperationCommandTool : public ContextActionTool {
public:
    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const final {
      const std::optional<ModelingActionItem> live = action_item(context);
      if (!live.has_value()) {
        return ModelingCommandAvailability::unavailable(
            "Command is not available in the current mode.");
      }
        if (live->visibility == ModelingActionVisibility::Enabled) {
            return ModelingCommandAvailability::available();
        }
        return ModelingCommandAvailability::unavailable(
            live->disabled_reason.empty()
                ? "Command is disabled for the current selection."
                : live->disabled_reason);
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
      return context.modeling.execute_context_action(action_id());
    }
};

} // namespace quader

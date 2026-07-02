////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/preview_command_tool.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace quader {

/**
 * Implements Preview Operation Command Tool behavior for the editor command and action routing layer.
 */
class PreviewOperationCommandTool : public PreviewCommandTool {
public:
  [[nodiscard]] std::optional<ModelingActionItem> action_item(
      const ModelingCommandExecutionContext &context,
      ModelingActionQuery query = default_action_query()) const override {
    const std::vector<ModelingActionItem> actions =
        context.modeling.context_actions(query);
    const auto found = std::find_if(actions.begin(), actions.end(),
                                    [&](const ModelingActionItem &item) {
                                      return item.action_id == action_id();
                                    });
    if (found == actions.end()) {
      return std::nullopt;
    }
    return *found;
  }

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

    ModelingCommandResult
    begin_preview(ModelingCommandExecutionContext &context) const final {
      return context.modeling.execute_context_action(action_id());
    }
};

} // namespace quader

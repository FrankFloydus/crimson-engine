////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/commands/command_tool.hpp"

#include <optional>
#include <string_view>

namespace quader {

/**
 * Implements Context Action Tool behavior for the editor command and action routing layer.
 */
class ContextActionTool : public CommandTool {
public:
  [[nodiscard]] ModelingCommandHistoryPolicy history_policy() const override {
    return rebuilds_mesh_on_success()
               ? ModelingCommandHistoryPolicy::modeling_undo_redo()
               : ModelingCommandHistoryPolicy::none();
  }

  [[nodiscard]] virtual std::string_view action_id() const = 0;

  [[nodiscard]] std::optional<ModelingActionItem>
  action_item(const ModelingCommandExecutionContext &context,
              ModelingActionQuery query) const {
    return context.modeling.context_action(action_id(), query);
  }

  [[nodiscard]] std::optional<ModelingActionItem>
  action_item(const ModelingCommandExecutionContext &context) const {
    return action_item(context, action_query());
  }

protected:
  [[nodiscard]] virtual ModelingActionQuery action_query() const {
    return default_action_query();
  }

  [[nodiscard]] static ModelingActionQuery default_action_query() {
    return {
        .surface = ModelingActionSurface::OperationsPanel,
        .modal_tool_owns_viewport = false,
        .include_disabled = true,
        .include_hidden = false,
    };
  }
};

} // namespace quader

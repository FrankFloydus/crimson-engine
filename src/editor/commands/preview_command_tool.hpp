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
 * Implements Preview Command Tool behavior for the editor command and action routing layer.
 */
class PreviewCommandTool : public CommandTool {
public:
  [[nodiscard]] ModelingCommandHistoryPolicy history_policy() const override {
    return ModelingCommandHistoryPolicy::none();
  }

  [[nodiscard]] virtual std::string_view action_id() const = 0;
  [[nodiscard]] virtual std::optional<ModelingActionItem>
  action_item(const ModelingCommandExecutionContext &context,
              ModelingActionQuery query = default_action_query()) const = 0;
  virtual ModelingCommandResult
  begin_preview(ModelingCommandExecutionContext &context) const = 0;

  ModelingCommandResult
  execute(ModelingCommandExecutionContext &context) const final {
    return begin_preview(context);
  }

  ModelingCommandResult begin_preview(ModelingSessionService &modeling) const {
    ModelingCommandExecutionContext context{.modeling = modeling};
    return begin_preview(context);
  }

  ModelingCommandResult commit_preview(ModelingSessionService &modeling) const {
    return modeling.commit_active_operation_preview();
  }

  ModelingCommandResult cancel_preview(ModelingSessionService &modeling) const {
    return modeling.cancel_active_operation_preview();
  }

  ModelingCommandResult
  apply_settings(ModelingSessionService &modeling,
                 ModelingInteractionSettings settings) const {
    return modeling.set_interaction_settings(settings);
  }

protected:
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

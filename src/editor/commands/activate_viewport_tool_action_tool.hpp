////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/context_action_tool.hpp"

#include <string>
#include <string_view>

namespace quader {

/**
 * Implements Activate Viewport Tool Action Tool behavior for the editor command and action routing layer.
 */
class ActivateViewportToolActionTool final : public ContextActionTool {
public:
  ActivateViewportToolActionTool(ModelingViewportTool viewport_tool,
                                 std::string_view action_id,
                                 std::string_view label,
                                 std::string_view operation_id)
      : viewport_tool_(viewport_tool), action_id_(action_id),
        operation_id_(operation_id), label_(label) {}

  [[nodiscard]] CommandToolDescriptor descriptor() const override {
    return {
        .id = action_id_,
        .operation_id = operation_id_,
        .label = label_,
        .kind = CommandToolKind::ContextAction,
        .preview_kind = ModelingActionPreviewKind::None,
        .rebuilds_mesh_on_success = false,
    };
  }

  [[nodiscard]] std::string_view action_id() const override {
    return action_id_;
  }

  [[nodiscard]] ModelingViewportTool viewport_tool() const {
    return viewport_tool_;
  }

    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
        (void)context;
        return ModelingCommandAvailability::available();
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const override {
      return context.modeling.activate_viewport_tool(viewport_tool_);
    }

private:
  ModelingViewportTool viewport_tool_ = ModelingViewportTool::Select;
  std::string action_id_;
  std::string operation_id_;
  std::string label_;
};

} // namespace quader

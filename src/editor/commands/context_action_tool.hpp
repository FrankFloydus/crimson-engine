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
    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy() const override {
        return rebuildsMeshOnSuccess() ? ModelingCommandHistoryPolicy::modelingUndoRedo() :
                                         ModelingCommandHistoryPolicy::none();
    }

    [[nodiscard]] virtual std::string_view actionId() const = 0;

    [[nodiscard]] std::optional<ModelingActionItem> actionItem(
            const ModelingCommandExecutionContext& context,
            ModelingActionQuery query) const {
        return context.modeling.contextAction(actionId(), query);
    }

    [[nodiscard]] std::optional<ModelingActionItem> actionItem(
            const ModelingCommandExecutionContext& context) const {
        return actionItem(context, actionQuery());
    }

protected:
    [[nodiscard]] virtual ModelingActionQuery actionQuery() const {
        return defaultActionQuery();
    }

    [[nodiscard]] static ModelingActionQuery defaultActionQuery() {
        return {
                .surface = ModelingActionSurface::OperationsPanel,
                .modalToolOwnsViewport = false,
                .includeDisabled = true,
                .includeHidden = false,
        };
    }
};

} // namespace quader

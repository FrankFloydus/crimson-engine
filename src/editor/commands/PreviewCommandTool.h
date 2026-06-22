////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/commands/CommandTool.h"

#include <optional>
#include <string_view>

namespace quader {

/**
 * Implements Preview Command Tool behavior for the editor command and action routing layer.
 */
class PreviewCommandTool : public CommandTool {
public:
    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy() const override {
        return ModelingCommandHistoryPolicy::none();
    }

    [[nodiscard]] virtual std::string_view actionId() const = 0;
    [[nodiscard]] virtual std::optional<ModelingActionItem> actionItem(
            const ModelingCommandExecutionContext& context,
            ModelingActionQuery query = defaultActionQuery()) const = 0;
    virtual ModelingCommandResult beginPreview(ModelingCommandExecutionContext& context) const = 0;

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
        return beginPreview(context);
    }

    ModelingCommandResult beginPreview(ModelingSessionService& modeling) const {
        ModelingCommandExecutionContext context{.modeling = modeling};
        return beginPreview(context);
    }

    ModelingCommandResult commitPreview(ModelingSessionService& modeling) const {
        return modeling.commitActiveOperationPreview();
    }

    ModelingCommandResult cancelPreview(ModelingSessionService& modeling) const {
        return modeling.cancelActiveOperationPreview();
    }

    ModelingCommandResult applySettings(ModelingSessionService& modeling, ModelingInteractionSettings settings) const {
        return modeling.setInteractionSettings(settings);
    }

protected:
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

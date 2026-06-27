////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/CommandTool.h"

#include <optional>

namespace quader {

/**
 * Implements Instant Command Tool behavior for the editor command and action routing layer.
 */
class InstantCommandTool : public CommandTool {
public:
    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy() const override {
        return rebuildsMeshOnSuccess() ? ModelingCommandHistoryPolicy::modelingUndoRedo() :
                                         ModelingCommandHistoryPolicy::none();
    }

    [[nodiscard]] virtual std::optional<ModelingInstantOperationId> instantOperation() const {
        return std::nullopt;
    }

    [[nodiscard]] bool hasInstantOperation() const { return instantOperation().has_value(); }
    [[nodiscard]] ModelingInstantOperationId operation() const {
        return instantOperation().value();
    }

    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
        (void)context;
        return ModelingCommandAvailability::available();
    }
};

} // namespace quader

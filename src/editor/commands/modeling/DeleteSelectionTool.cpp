////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/DeleteSelectionTool.h"

#include "editor/modeling/services/ModelingSessionService.h"

namespace quader {
namespace {

[[nodiscard]] bool sceneAwareDeleteOwnsMode(ModelingSelectionMode mode) {
    return mode == ModelingSelectionMode::Object || mode == ModelingSelectionMode::Group;
}

} // namespace

ModelingCommandAvailability DeleteSelectionTool::customAvailability(
        const ModelingCommandExecutionContext& context) const {
    const ModelingSelectionSummary summary = context.modeling.selectionSummary();
    if (!summary.hasSelection) {
        return ModelingCommandAvailability::unavailable("Command is disabled for the current selection.");
    }
    if (sceneAwareDeleteOwnsMode(summary.mode)) {
        return ModelingCommandAvailability::unavailable("Delete selection uses the scene-aware command route.");
    }
    return ModelingCommandAvailability::available();
}

ModelingCommandResult DeleteSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.deleteComponentSelection();
}

} // namespace quader

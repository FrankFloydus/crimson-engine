////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/delete_selection_tool.hpp"

#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

namespace quader {

ModelingCommandAvailability DeleteSelectionTool::customAvailability(
        const ModelingCommandExecutionContext& context) const {
    const ModelingSelectionSummary summary = context.modeling.selectionSummary();
    if (!summary.hasSelection) {
        return ModelingCommandAvailability::unavailable("Command is disabled for the current selection.");
    }
    if (sceneAwareDeleteUsesDocumentRoute(summary.mode)) {
        return ModelingCommandAvailability::unavailable("Delete selection uses the scene-aware command route.");
    }
    return ModelingCommandAvailability::available();
}

ModelingCommandResult DeleteSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.deleteComponentSelection();
}

} // namespace quader

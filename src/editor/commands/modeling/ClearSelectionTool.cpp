////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/ClearSelectionTool.h"

#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/selection/EditorSelectionService.h"

namespace quader {

ModelingCommandResult ClearSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    if (context.selection != nullptr) {
        return context.selection->clear();
    }
    return context.modeling.clearSelection();
}

} // namespace quader

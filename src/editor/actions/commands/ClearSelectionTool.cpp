////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/actions/commands/ClearSelectionTool.h"

#include "modeling/services/ModelingSessionService.h"

namespace quader {

ModelingCommandResult ClearSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    return context.modeling.clearSelection();
}

} // namespace quader

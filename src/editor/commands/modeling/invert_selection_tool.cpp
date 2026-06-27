////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/invert_selection_tool.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

namespace quader {

ModelingCommandResult InvertSelectionTool::executeCustomCommand(ModelingCommandExecutionContext& context) const {
    if (context.selection != nullptr) {
        return context.selection->invert();
    }
    return context.modeling.invertSelection();
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/select_all_tool.hpp"

#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

namespace quader {

ModelingCommandResult SelectAllTool::execute_custom_command(ModelingCommandExecutionContext& context) const {
    QDR_PROFILE_FUNCTION();
    if (context.selection != nullptr) {
      return context.selection->select_all();
    }
    return context.modeling.select_all();
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/paste_selection_tool.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"

namespace quader {

ModelingCommandAvailability PasteSelectionTool::custom_availability(
    const ModelingCommandExecutionContext &context) const {
  return context.modeling.can_paste_selection()
             ? ModelingCommandAvailability::available()
             : ModelingCommandAvailability::unavailable(
                   "Copy objects and finish the active preview before pasting.");
}

ModelingCommandResult PasteSelectionTool::execute_custom_command(ModelingCommandExecutionContext& context) const {
  return context.modeling.paste_selection();
}

} // namespace quader

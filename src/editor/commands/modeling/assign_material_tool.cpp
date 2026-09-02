////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/assign_material_tool.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"

namespace quader {

ModelingCommandAvailability AssignMaterialTool::custom_availability(
    const ModelingCommandExecutionContext &context) const {
  const ModelingCommandResult status =
      context.modeling.active_material_selection_status(
          context.active_material.valid());
  return status.ok
             ? ModelingCommandAvailability::available()
             : ModelingCommandAvailability::unavailable(status.message);
}

ModelingCommandResult AssignMaterialTool::execute_custom_command(
    ModelingCommandExecutionContext &context) const {
  return context.active_material.valid()
             ? context.modeling.apply_active_material(context.active_material)
             : context.modeling.apply_active_material();
}

} // namespace quader

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
  const ModelingSelectionSummary summary =
      context.modeling.selection_summary();
  if (summary.mode != ModelingSelectionMode::Object &&
      summary.mode != ModelingSelectionMode::Face) {
    return ModelingCommandAvailability::unavailable(
        "Apply Material works in object or face mode.");
  }
  if ((summary.mode == ModelingSelectionMode::Object &&
       summary.selected_object_count == 0u) ||
      (summary.mode == ModelingSelectionMode::Face &&
       summary.selected_face_count == 0u)) {
    return ModelingCommandAvailability::unavailable(
        summary.mode == ModelingSelectionMode::Object
            ? "Select one or more meshes before applying material."
            : "Select one or more faces before applying material.");
  }
  return ModelingCommandAvailability::available();
}

ModelingCommandResult AssignMaterialTool::execute_custom_command(
    ModelingCommandExecutionContext &context) const {
  return context.active_material.valid()
             ? context.modeling.apply_active_material(context.active_material)
             : context.modeling.apply_active_material();
}

} // namespace quader

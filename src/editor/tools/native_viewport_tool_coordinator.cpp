////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/native_viewport_tool_coordinator.hpp"

#include "editor/array/linear_array_tool_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

namespace quader {

NativeViewportToolCoordinator::NativeViewportToolCoordinator(
    PivotToolService &pivot, LinearArrayToolService &linear_array,
    EditorSelectionService &selection)
    : pivot_(pivot), linear_array_(linear_array), selection_(selection) {}

bool NativeViewportToolCoordinator::handles(std::string_view tool_id) const {
  return tool_id == kPivotToolId || tool_id == kLinearArrayToolId;
}

bool NativeViewportToolCoordinator::can_activate(
    std::string_view tool_id) const {
  if (tool_id == kLinearArrayToolId) {
    return linear_array_.can_activate();
  }
  if (tool_id == kPivotToolId) {
    return pivot_.can_activate_persistent(selection_);
  }
  return false;
}

bool NativeViewportToolCoordinator::has_pending_tool() const {
  return linear_array_.has_session() || pivot_.persistent_active();
}

std::string_view NativeViewportToolCoordinator::active_tool_id() const {
  if (linear_array_.active()) {
    return kLinearArrayToolId;
  }
  if (pivot_.persistent_active()) {
    return kPivotToolId;
  }
  return {};
}

CommandResult NativeViewportToolCoordinator::activate(
    std::string_view tool_id, double grid_world_size) {
  if (tool_id == kLinearArrayToolId) {
    (void)pivot_.cancel_persistent();
    return linear_array_.activate(grid_world_size);
  }
  if (tool_id == kPivotToolId) {
    (void)linear_array_.cancel();
    return pivot_.activate_persistent(selection_);
  }
  return command_invalid_result("Unknown native viewport tool.");
}

CommandResult NativeViewportToolCoordinator::commit_active() {
  if (linear_array_.has_session()) {
    return linear_array_.apply();
  }
  if (pivot_.persistent_active()) {
    return pivot_.apply_persistent();
  }
  return command_invalid_result("No active native tool preview.");
}

CommandResult NativeViewportToolCoordinator::cancel_active() {
  if (linear_array_.has_session()) {
    return linear_array_.cancel();
  }
  if (pivot_.persistent_active()) {
    return pivot_.cancel_persistent();
  }
  return command_unchanged_result();
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/native_viewport_tool_coordinator.hpp"

#include "editor/array/linear_array_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"

namespace quader {

NativeViewportToolCoordinator::NativeViewportToolCoordinator(
    PivotToolService &pivot, LinearArrayToolService &linear_array,
    EditorSelectionService &selection, ModelingSessionService &modeling)
    : pivot_(pivot), linear_array_(linear_array), selection_(selection),
      modeling_(modeling) {}

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
  if (!handles(tool_id)) {
    return command_invalid_result("Unknown native viewport tool.");
  }
  if (tool_id == kLinearArrayToolId) {
    if (linear_array_.active()) {
      return command_unchanged_result();
    }
  } else if (tool_id == kPivotToolId) {
    if (pivot_.persistent_active()) {
      return command_unchanged_result();
    }
  }
  if (!can_activate(tool_id)) {
    return command_invalid_result(
        tool_id == kLinearArrayToolId
            ? "Select exactly one visible, unlocked mesh in object mode."
            : "Select one visible, unlocked mesh or group in object mode.");
  }

  const CommandResult canceled =
      cancel_competing_state_for_tool_activation();
  if (!canceled.ok) {
    return canceled;
  }

  CommandResult activated =
      tool_id == kLinearArrayToolId
          ? linear_array_.activate(grid_world_size)
          : pivot_.activate_persistent(selection_);
  activated.changed = activated.changed || canceled.changed;
  return activated;
}

CommandResult
NativeViewportToolCoordinator::cancel_competing_state_for_tool_activation() {
  const ModelingCommandResult modeling_cancel =
      modeling_.cancel_competing_state_for_native_tool_activation();
  if (!modeling_cancel.ok) {
    return {
        .ok = false,
        .changed = modeling_cancel.changed,
        .message = modeling_cancel.message,
    };
  }

  const CommandResult native_cancel = cancel_prior_native_state();
  if (!native_cancel.ok) {
    return {
        .ok = false,
        .changed = modeling_cancel.changed || native_cancel.changed,
        .message = native_cancel.message,
    };
  }
  return {
      .ok = true,
      .changed = modeling_cancel.changed || native_cancel.changed,
  };
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
  return cancel_prior_native_state();
}

CommandResult NativeViewportToolCoordinator::cancel_prior_native_state() {
  const CommandResult linear_array = linear_array_.cancel();
  if (!linear_array.ok) {
    return linear_array;
  }
  const CommandResult pivot = pivot_.cancel_persistent();
  if (!pivot.ok) {
    return {
        .ok = false,
        .changed = linear_array.changed || pivot.changed,
        .message = pivot.message,
    };
  }
  return {
      .ok = true,
      .changed = linear_array.changed || pivot.changed,
  };
}

void NativeViewportToolCoordinator::reset_transient_state() {
  (void)linear_array_.cancel();
  (void)pivot_.cancel_persistent();
  pivot_.clear_quick_override();
}

} // namespace quader

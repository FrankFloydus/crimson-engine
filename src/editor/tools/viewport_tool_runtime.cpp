////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/viewport_tool_runtime.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"

namespace quader {

std::span<const ViewportTool* const> ViewportToolRegistry::tools() const {
  return viewport_tool_catalog();
}

const ViewportTool* ViewportToolRegistry::find(ViewportToolId tool) const {
  return find_viewport_tool(tool);
}

const ViewportTool* ViewportToolRegistry::find(std::string_view id) const {
  return find_viewport_tool(id);
}

bool ViewportToolRegistry::available(ViewportToolId tool) const {
    const ViewportTool* item = find(tool);
    return item != nullptr &&
           viewport_tool_has_native_controller(item->viewport_tool());
}

bool ViewportToolHost::available(const ViewportTool& tool) const {
  return viewport_tool_has_native_controller(tool.viewport_tool());
}

bool ViewportToolHost::owns_viewport(const ViewportTool &tool) const {
  return tool.owns_viewport();
}

bool ViewportToolHost::blocks_viewport_focus_shortcuts(
    const ViewportTool &tool) const {
  return tool.blocks_viewport_focus_shortcuts();
}

bool ViewportToolHost::allows_selection_fallback(
    const ViewportTool &tool) const {
  return tool.allows_selection_fallback();
}

ModelingCommandResult
ViewportToolHost::activate_tool(const ViewportTool &tool,
                                ModelingSessionService &modeling) const {
  return tool.activate(modeling);
}

ViewportToolEvent ViewportToolEventBuilder::activation_event(
    ViewportToolId tool, const ViewportShortcut *shortcut) const {
  ViewportToolEvent event;
  event.tool = tool;
  if (shortcut != nullptr) {
    event.shortcut = *shortcut;
    event.from_shortcut = true;
  }
  return event;
}

bool ViewportToolController::activate_tool(const ViewportToolRegistry &registry,
                                           const ViewportToolHost &host,
                                           ViewportToolState &state,
                                           ViewportToolId tool,
                                           const ViewportTool *&active_tool,
                                           ViewportInputResult *result) const {
  const ViewportTool *next_tool = registry.find(tool);
  if (next_tool == nullptr || !host.available(*next_tool)) {
    return false;
  }

  const bool changed = state.active_tool != tool;
  state.active_tool = tool;
  active_tool = next_tool;
  if (result != nullptr) {
    result->tool_changed = result->tool_changed || changed;
  }
  return true;
}

bool ViewportModalToolController::handles(const ViewportTool& tool) const {
    return tool.kind() == ToolKind::Modal;
}

bool ViewportModalToolController::owns_viewport(
    const ViewportToolHost &host, const ViewportTool &tool) const {
  return handles(tool) && host.owns_viewport(tool);
}

bool ViewportModalToolController::blocks_viewport_focus_shortcuts(
    const ViewportToolHost &host, const ViewportTool &tool) const {
  return handles(tool) && host.blocks_viewport_focus_shortcuts(tool);
}

bool ViewportTransformToolController::handles(const ViewportTool& tool) const {
    return tool.kind() == ToolKind::Transform;
}

ModelingTransformTool ViewportTransformToolController::transform_tool(
    const ViewportTool &tool) const {
  return handles(tool) ? tool.transform_tool() : ModelingTransformTool::Move;
}

ViewportToolRuntime::ViewportToolRuntime() {
  sync_active_tool(ViewportToolId::Select);
}

bool ViewportToolRuntime::active_tool_owns_viewport() const {
  return active_tool_ != nullptr &&
         modal_controller_.owns_viewport(host_, *active_tool_);
}

bool ViewportToolRuntime::active_tool_blocks_viewport_focus_shortcuts() const {
  return active_tool_ != nullptr &&
         modal_controller_.blocks_viewport_focus_shortcuts(host_,
                                                           *active_tool_);
}

bool ViewportToolRuntime::tool_available(ViewportToolId tool) const {
  return registry_.available(tool);
}

bool ViewportToolRuntime::tool_allows_selection_fallback(
    ViewportToolId tool) const {
  const ViewportTool *item = registry_.find(tool);
  return item != nullptr && host_.allows_selection_fallback(*item);
}

ModelingTransformTool
ViewportToolRuntime::transform_tool(ViewportToolId tool) const {
  const ViewportTool *item = registry_.find(tool);
  return item != nullptr ? transform_controller_.transform_tool(*item)
                         : ModelingTransformTool::Move;
}

bool ViewportToolRuntime::set_active_tool(ViewportToolState &state,
                                          ViewportToolId tool,
                                          ViewportInputResult *result) {
  return controller_.activate_tool(registry_, host_, state, tool, active_tool_,
                                   result);
}

void ViewportToolRuntime::sync_active_tool(ViewportToolId tool) {
  active_tool_ = registry_.find(tool);
}

bool ViewportToolRuntime::has_active_modal_preview(
    const ViewportToolPreviewPort &preview, bool snap_enabled) const {
  const ModelingBoxPreview box = box_preview(preview, snap_enabled);
  return box.valid || box.drag_state != ModelingBoxDragState::Idle;
}

ModelingBoxPreview
ViewportToolRuntime::box_preview(const ViewportToolPreviewPort &preview,
                                 bool snap_enabled) const {
  return preview.box_preview(snap_enabled);
}

void ViewportToolRuntime::clear_modal_tool_state() { modal_state_ = {}; }

void ViewportToolRuntime::clear_modal_drag_state() {
  modal_state_.insert_edge_loop_drag_active = false;
  modal_state_.cut_drag_active = false;
  modal_state_.mirror_drag_active = false;
}

bool ViewportToolRuntime::handle_shortcut(const ViewportShortcut &shortcut,
                                          ViewportToolState &state,
                                          ViewportCameraController &camera,
                                          ViewportInputResult &result) {
  (void)event_builder_.activation_event(shortcut.tool, &shortcut);

  switch (shortcut.kind) {
  case ViewportShortcutKind::ActivateTool:
    if (!set_active_tool(state, shortcut.tool, &result)) {
      return false;
    }
    result.tool_activation_requested = true;
    return true;
  case ViewportShortcutKind::SetSelectionMode:
    result.selection_mode_changed =
        result.selection_mode_changed ||
        state.selection_mode != shortcut.selection_mode;
    state.selection_mode = shortcut.selection_mode;
    return true;
  case ViewportShortcutKind::FocusDocument:
    if (active_tool_blocks_viewport_focus_shortcuts()) {
      return false;
    }
    result.focus_document_requested = true;
    result.camera_changed = true;
    return true;
  case ViewportShortcutKind::FocusOrigin:
    camera.focus_origin();
    result.camera_changed = true;
    return true;
  case ViewportShortcutKind::ToggleMaximized:
    if (active_tool_blocks_viewport_focus_shortcuts()) {
      return false;
    }
    result.maximized_toggle_requested = true;
    return true;
  case ViewportShortcutKind::SetGridPreset:
    result.requested_grid_preset =
        viewport_clamp_grid_preset(shortcut.grid_preset);
    result.grid_preset_changed =
        state.grid_preset != result.requested_grid_preset;
    return true;
  case ViewportShortcutKind::SetView:
    result.requested_view = shortcut.view;
    result.view_changed = true;
    return true;
  case ViewportShortcutKind::CancelActiveTool:
    result.cancel_requested = true;
    return true;
  case ViewportShortcutKind::DeletePopup:
    return !active_tool_blocks_viewport_focus_shortcuts();
  case ViewportShortcutKind::DeleteSelection:
  case ViewportShortcutKind::SelectAll:
  case ViewportShortcutKind::InvertSelection:
  case ViewportShortcutKind::Copy:
  case ViewportShortcutKind::Paste:
  case ViewportShortcutKind::Duplicate:
  case ViewportShortcutKind::Undo:
  case ViewportShortcutKind::Redo:
  case ViewportShortcutKind::RepeatLastAction:
  case ViewportShortcutKind::MergeActive:
  case ViewportShortcutKind::MergeCenter:
  case ViewportShortcutKind::MirrorAxis:
    return true;
  case ViewportShortcutKind::FlipMeshNormals:
    if (active_tool_blocks_viewport_focus_shortcuts()) {
      return false;
    }
    return state.selection_mode == ViewportSelectionMode::Object;
  case ViewportShortcutKind::FlipHorizontal:
  case ViewportShortcutKind::FlipVertical:
    return state.selection_mode == ViewportSelectionMode::Object;
  case ViewportShortcutKind::ApplyMaterial:
    return true;
  case ViewportShortcutKind::None:
    return false;
  }
  return false;
}

ViewportToolResult
ViewportToolRuntime::handle_active_tool_event(ViewportToolState &state,
                                              ViewportToolContext &context,
                                              const ViewportToolEvent &event) {
  if (active_tool_ == nullptr) {
    sync_active_tool(state.active_tool);
  }
  if (active_tool_ == nullptr) {
    return {};
  }
  context.modal_state = &modal_state_;
  ViewportToolResult result = active_tool_->handle_event(context, event);
  if (result.sync_active_tool_from_modeling) {
    if (context.ports.preview == nullptr) {
      return result;
    }
    const ViewportToolId next_tool =
        viewport_tool_for_modeling(
            context.ports.preview->tool_status().active_tool);
    const bool changed = state.active_tool != next_tool;
    if (set_active_tool(state, next_tool)) {
      result.active_tool_changed = result.active_tool_changed || changed;
    }
  }
  return result;
}

ModelingCommandResult ViewportToolRuntime::activate_active_tool(
    ModelingSessionService &modeling) const {
  if (active_tool_ == nullptr) {
    return {
        .ok = false,
        .changed = false,
        .message = "No active viewport tool.",
    };
  }
  return host_.activate_tool(*active_tool_, modeling);
}

} // namespace quader

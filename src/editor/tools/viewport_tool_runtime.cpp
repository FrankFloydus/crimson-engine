////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/viewport_tool_runtime.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <algorithm>
#include <utility>

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

bool ViewportToolRuntime::tool_uses_editor_native_lifecycle(
    ViewportToolId tool) const {
  const ViewportTool *item = registry_.find(tool);
  return item != nullptr &&
         item->descriptor().editor_owned_native_lifecycle;
}

ViewportPointerCursor
ViewportToolRuntime::tool_pointer_cursor(ViewportToolId tool) const {
  const ViewportTool *item = registry_.find(tool);
  return item != nullptr ? item->pointer_cursor()
                         : ViewportPointerCursor::Default;
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

void ViewportToolRuntime::clear_modal_tool_state() {
  ViewportPointerReleaseSuppression release_suppression =
      modal_state_.primary_release_suppression;
  if (modal_state_.primary_pointer_gesture) {
    // Tool/lifecycle changes can precede the physical button-up edge. Retain
    // that edge at the runtime boundary even after the old tool is gone.
    release_suppression.begin(QuaderMouseButton::Left, true);
  }
  modal_state_ = {};
  modal_state_.primary_release_suppression = release_suppression;
}

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
  if (event.kind == ViewportToolEventKind::Wheel && !active_tool_->handles_wheel_events()) {
    return {};
  }
  context.modal_state = &modal_state_;
  if (event.kind == ViewportToolEventKind::Frame &&
      active_tool_->viewport_tool() != ViewportToolId::CreatePrimitive &&
      modal_state_.primary_release_suppression.consume(
          QuaderMouseButton::Left, event.pointer.left_clicked,
          event.pointer.left_down, event.pointer.left_released)) {
    return {
        .handled = true,
        .consumed = true,
        .modal_state_changed = true,
    };
  }
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

bool ViewportToolRuntime::register_native_lifecycle(
    ViewportNativeToolLifecycleAdapter adapter) {
  if (adapter.local_history &&
      (adapter.local_history->label.empty() ||
       !adapter.local_history->can_undo || !adapter.local_history->can_redo ||
       !adapter.local_history->undo || !adapter.local_history->redo)) {
    return false;
  }
  const ViewportTool *tool = registry_.find(adapter.tool);
  if (tool == nullptr || !tool->descriptor().editor_owned_native_lifecycle ||
      native_lifecycle_registered(adapter.tool) || adapter.commit_label.empty() ||
      !adapter.can_activate ||
      !adapter.active || !adapter.pending ||
      !adapter.pending_history_preview || !adapter.activate ||
      !adapter.commit || !adapter.cancel || !adapter.reset) {
    return false;
  }
  native_lifecycles_.push_back(std::move(adapter));
  return true;
}

bool ViewportToolRuntime::native_lifecycle_registered(
    ViewportToolId tool) const {
  return find_native_lifecycle(tool) != nullptr;
}

bool ViewportToolRuntime::native_lifecycle_catalog_complete() const {
  const auto tools = registry_.tools();
  const auto declared_count =
      std::ranges::count_if(tools, [](const ViewportTool *tool) {
        return tool != nullptr &&
               tool->descriptor().editor_owned_native_lifecycle;
      });
  return native_lifecycles_.size() ==
             static_cast<std::size_t>(declared_count) &&
         std::ranges::all_of(tools, [&](const ViewportTool *tool) {
           return tool == nullptr ||
                  !tool->descriptor().editor_owned_native_lifecycle ||
                  native_lifecycle_registered(tool->viewport_tool());
         });
}

bool ViewportToolRuntime::handles_native_lifecycle(
    std::string_view tool_id) const {
  return find_native_lifecycle(tool_id) != nullptr;
}

bool ViewportToolRuntime::native_lifecycle_active(
    std::string_view tool_id) const {
  const ViewportNativeToolLifecycleAdapter *adapter =
      find_native_lifecycle(tool_id);
  return adapter != nullptr && adapter->active();
}

bool ViewportToolRuntime::native_lifecycle_pending(
    std::string_view tool_id) const {
  const ViewportNativeToolLifecycleAdapter *adapter =
      find_native_lifecycle(tool_id);
  return adapter != nullptr && adapter->pending();
}

bool ViewportToolRuntime::can_activate_native_lifecycle(
    std::string_view tool_id) const {
  const ViewportNativeToolLifecycleAdapter *adapter =
      find_native_lifecycle(tool_id);
  return adapter != nullptr && adapter->can_activate();
}

bool ViewportToolRuntime::has_pending_native_lifecycle() const {
  return std::ranges::any_of(native_lifecycles_, [](const auto &adapter) {
    return adapter.pending();
  });
}

bool ViewportToolRuntime::has_pending_native_history_preview() const {
  return std::ranges::any_of(native_lifecycles_, [](const auto &adapter) {
    return adapter.pending_history_preview();
  });
}

bool ViewportToolRuntime::native_lifecycle_blocks_modeling_content_mutation()
    const {
  return std::ranges::any_of(native_lifecycles_, [](const auto &adapter) {
    return adapter.blocks_modeling_content_mutation &&
           adapter.blocks_modeling_content_mutation();
  });
}

bool ViewportToolRuntime::native_lifecycle_blocks_document_mutation(
    EntityId entity) const {
  return std::ranges::any_of(native_lifecycles_, [&](const auto &adapter) {
    return adapter.blocks_document_mutation &&
           adapter.blocks_document_mutation(entity);
  });
}

std::string_view ViewportToolRuntime::active_native_lifecycle_id() const {
  const ViewportNativeToolLifecycleAdapter *active = active_native_lifecycle();
  if (active == nullptr) {
    return {};
  }
  const ViewportTool *tool = registry_.find(active->tool);
  return tool != nullptr ? tool->id() : std::string_view{};
}

std::optional<ViewportNativeToolCommitMetadata>
ViewportToolRuntime::active_native_lifecycle_commit_metadata() const {
  const auto found =
      std::ranges::find_if(native_lifecycles_, [](const auto &adapter) {
        return adapter.pending();
      });
  if (found == native_lifecycles_.end()) {
    return std::nullopt;
  }
  return ViewportNativeToolCommitMetadata{
      .label = found->commit_label,
      .domain = found->commit_domain,
  };
}

std::optional<ViewportNativeToolHistoryState>
ViewportToolRuntime::active_native_lifecycle_history_state() const {
  const ViewportNativeToolLifecycleAdapter *active = active_native_lifecycle();
  if (active == nullptr || !active->local_history) {
    return std::nullopt;
  }
  const ViewportNativeToolHistoryAdapter &history = *active->local_history;
  return ViewportNativeToolHistoryState{
      .can_undo = history.can_undo(),
      .can_redo = history.can_redo(),
      .label = history.label,
  };
}

CommandResult ViewportToolRuntime::activate_native_lifecycle(
    std::string_view tool_id, double grid_world_size,
    const std::function<CommandResult()> &cancel_competing_state) {
  ViewportNativeToolLifecycleAdapter *adapter =
      find_native_lifecycle(tool_id);
  if (adapter == nullptr) {
    return command_invalid_result("Unknown native viewport tool.");
  }
  if (adapter->active()) {
    return command_unchanged_result();
  }
  if (!adapter->can_activate()) {
    return command_invalid_result(adapter->unavailable_message.empty()
                                      ? "Native viewport tool is unavailable."
                                      : adapter->unavailable_message);
  }

  const CommandResult canceled = cancel_competing_state();
  if (!canceled.ok) {
    return canceled;
  }
  CommandResult activated = adapter->activate(grid_world_size);
  activated.changed = activated.changed || canceled.changed;
  return activated;
}

CommandResult ViewportToolRuntime::commit_active_native_lifecycle() {
  const auto found =
      std::ranges::find_if(native_lifecycles_, [](const auto &adapter) {
        return adapter.pending();
      });
  return found != native_lifecycles_.end()
             ? found->commit()
             : command_invalid_result("No active native tool preview.");
}

CommandResult ViewportToolRuntime::undo_active_native_lifecycle() {
  const ViewportNativeToolLifecycleAdapter *active = active_native_lifecycle();
  return active != nullptr && active->local_history
             ? active->local_history->undo()
             : command_invalid_result("No active native tool history.");
}

CommandResult ViewportToolRuntime::redo_active_native_lifecycle() {
  const ViewportNativeToolLifecycleAdapter *active = active_native_lifecycle();
  return active != nullptr && active->local_history
             ? active->local_history->redo()
             : command_invalid_result("No active native tool history.");
}

CommandResult ViewportToolRuntime::cancel_native_lifecycles() {
  bool changed = false;
  for (ViewportNativeToolLifecycleAdapter &adapter : native_lifecycles_) {
    CommandResult canceled = adapter.cancel();
    changed = changed || canceled.changed;
    if (!canceled.ok) {
      canceled.changed = changed;
      return canceled;
    }
  }
  return {.ok = true, .changed = changed};
}

void ViewportToolRuntime::reset_native_lifecycles() {
  for (ViewportNativeToolLifecycleAdapter &adapter : native_lifecycles_) {
    adapter.reset();
  }
}

const ViewportNativeToolLifecycleAdapter *
ViewportToolRuntime::active_native_lifecycle() const {
  const ViewportNativeToolLifecycleAdapter *active = nullptr;
  for (const ViewportNativeToolLifecycleAdapter &adapter : native_lifecycles_) {
    if (adapter.active() &&
        (active == nullptr ||
         adapter.active_priority < active->active_priority)) {
      active = &adapter;
    }
  }
  return active;
}

ViewportNativeToolLifecycleAdapter *
ViewportToolRuntime::find_native_lifecycle(std::string_view tool_id) {
  const ViewportTool *tool = registry_.find(tool_id);
  if (tool == nullptr) {
    return nullptr;
  }
  const auto found =
      std::ranges::find(native_lifecycles_, tool->viewport_tool(),
                        &ViewportNativeToolLifecycleAdapter::tool);
  return found == native_lifecycles_.end() ? nullptr : &(*found);
}

const ViewportNativeToolLifecycleAdapter *
ViewportToolRuntime::find_native_lifecycle(std::string_view tool_id) const {
  const ViewportTool *tool = registry_.find(tool_id);
  return tool != nullptr ? find_native_lifecycle(tool->viewport_tool()) : nullptr;
}

const ViewportNativeToolLifecycleAdapter *
ViewportToolRuntime::find_native_lifecycle(ViewportToolId tool) const {
  const auto found =
      std::ranges::find(native_lifecycles_, tool,
                        &ViewportNativeToolLifecycleAdapter::tool);
  return found == native_lifecycles_.end() ? nullptr : &(*found);
}

} // namespace quader

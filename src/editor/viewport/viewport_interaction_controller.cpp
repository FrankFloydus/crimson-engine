////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_interaction_controller.hpp"

#include "diagnostics/profile.hpp"
#include "editor/tools/viewport_tool.hpp"

namespace quader {

ViewportBeforeToolInteractionResult
ViewportInteractionController::handle_before_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  QDR_PROFILE_FUNCTION();
  return transform_gizmo_.handle_frame(editor, frame);
}

void ViewportInteractionController::handle_after_active_tool(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
    const ViewportToolResult &tool_result,
    const ViewportBeforeToolInteractionResult &before_tool,
    ViewportToolHoverPort *tool_hover) {
  QDR_PROFILE_FUNCTION();
  selection_.handle_frame(editor, frame, tool_result, before_tool, tool_hover);
}

QuaderSelectionSummary ViewportInteractionController::pick_context_menu_target(
    QuaderEditorApi &editor, const ViewportInteractionFrame &frame) {
  return selection_.pick_context_menu_target(editor, frame);
}

bool ViewportInteractionController::cancel_active_drag(
    QuaderEditorApi &editor) {
  return transform_gizmo_.cancel_active_drag(editor);
}

bool ViewportInteractionController::dragging() const {
  return transform_gizmo_.dragging();
}

void ViewportInteractionController::reset(QuaderEditorApi* editor) {
  transform_gizmo_.reset(editor);
  selection_.reset();
}

const quader_editor_core::TransformGizmoFrame *
ViewportInteractionController::transform_gizmo_frame() const {
  return transform_gizmo_.frame();
}

const TransformGizmoSelectionCacheStats &
ViewportInteractionController::transform_gizmo_selection_cache_stats() const {
  return transform_gizmo_.selection_cache_stats();
}

ViewportSelectionMarquee
ViewportInteractionController::selection_marquee() const {
  return selection_.marquee();
}

void ViewportInteractionController::sync_tool_runtime(ViewportToolId tool) {
  if (tool_runtime_.active_tool() == nullptr ||
      tool_runtime_.active_tool()->viewport_tool() != tool) {
    tool_runtime_.sync_active_tool(tool);
  }
}

bool ViewportInteractionController::active_tool_owns_viewport(
    ViewportToolId tool) {
  sync_tool_runtime(tool);
  return tool_runtime_.active_tool_owns_viewport();
}

bool ViewportInteractionController::active_tool_blocks_viewport_focus_shortcuts(
    ViewportToolId tool) {
  sync_tool_runtime(tool);
  return tool_runtime_.active_tool_blocks_viewport_focus_shortcuts();
}

bool ViewportInteractionController::active_tool_allows_selection_fallback(
    ViewportToolId tool) const {
  return tool_runtime_.tool_allows_selection_fallback(tool);
}

bool ViewportInteractionController::has_active_modal_preview(
    const ViewportToolPreviewPort &preview, bool snap_enabled) const {
  return tool_runtime_.has_active_modal_preview(preview, snap_enabled);
}

ModelingBoxPreview ViewportInteractionController::box_preview(
    const ViewportToolPreviewPort &preview, bool snap_enabled) const {
  return tool_runtime_.box_preview(preview, snap_enabled);
}

const ViewportTool *
ViewportInteractionController::active_tool(ViewportToolId tool) {
  sync_tool_runtime(tool);
  return tool_runtime_.active_tool();
}

bool ViewportInteractionController::set_active_tool(
    ViewportToolState &state, ViewportToolId tool, ViewportInputResult *result) {
  return tool_runtime_.set_active_tool(state, tool, result);
}

void ViewportInteractionController::clear_modal_tool_state() {
  tool_runtime_.clear_modal_tool_state();
}

void ViewportInteractionController::clear_modal_drag_state() {
  tool_runtime_.clear_modal_drag_state();
}

ViewportToolResult ViewportInteractionController::handle_active_tool_event(
    ViewportToolState &state, ViewportToolContext &context,
    const ViewportToolEvent &event) {
  sync_tool_runtime(state.active_tool);
  return tool_runtime_.handle_active_tool_event(state, context, event);
}

ViewportNativeToolInteractionResult
ViewportInteractionController::handle_native_tools(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    PivotToolService *pivot, LinearArrayToolService *linear_array,
    RadialArrayToolService *radial_array,
    ViewportPointPlacementService *point_placement,
    ViewportCameraController &camera,
    const ViewportNativeToolInteractionFrame &frame) {
  return native_tools_.handle_frame(editor, document, assets, pivot,
                                    linear_array, radial_array,
                                    point_placement, camera, frame);
}

void ViewportInteractionController::clear_native_tool_drag_state() {
  native_tools_.clear_drag_state();
}

bool ViewportInteractionController::native_tool_dragging() const {
  return native_tools_.dragging();
}

} // namespace quader

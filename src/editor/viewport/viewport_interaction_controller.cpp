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
    const ViewportBeforeToolInteractionResult &before_tool) {
  QDR_PROFILE_FUNCTION();
  selection_.handle_frame(editor, frame, tool_result, before_tool);
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

ViewportSelectionMarquee
ViewportInteractionController::selection_marquee() const {
  return selection_.marquee();
}

} // namespace quader

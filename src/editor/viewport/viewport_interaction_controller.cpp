////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_interaction_controller.hpp"

#include "editor/tools/viewport_tool.hpp"

namespace quader {

ViewportBeforeToolInteractionResult ViewportInteractionController::handleBeforeActiveTool(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame) {
    return transformGizmo_.handleFrame(editor, frame);
}

void ViewportInteractionController::handleAfterActiveTool(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame,
        const ViewportToolResult& toolResult,
        const ViewportBeforeToolInteractionResult& beforeTool) {
    selection_.handleFrame(editor, frame, toolResult, beforeTool.gizmoConsumed);
}

QuaderSelectionSummary ViewportInteractionController::pickContextMenuTarget(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame) {
    return selection_.pickContextMenuTarget(editor, frame);
}

bool ViewportInteractionController::cancelActiveDrag(QuaderEditorApi& editor) {
    return transformGizmo_.cancelActiveDrag(editor);
}

bool ViewportInteractionController::dragging() const {
    return transformGizmo_.dragging();
}

void ViewportInteractionController::reset() {
    selection_.reset();
}

const quader_editor_core::TransformGizmoFrame* ViewportInteractionController::transformGizmoFrame() const {
    return transformGizmo_.frame();
}

ViewportSelectionMarquee ViewportInteractionController::selectionMarquee() const {
    return selection_.marquee();
}

} // namespace quader

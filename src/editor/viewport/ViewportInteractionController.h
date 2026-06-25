////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/viewport/TransformGizmoInteraction.h"
#include "editor/viewport/ViewportSelectionInteraction.h"

namespace quader {

class QuaderEditorApi;
struct ViewportToolResult;

/**
 * Coordinates editor-owned viewport interactions around active viewport tools.
 */
class ViewportInteractionController {
public:
    [[nodiscard]] ViewportBeforeToolInteractionResult handleBeforeActiveTool(
            QuaderEditorApi& editor,
            const ViewportInteractionFrame& frame);
    void handleAfterActiveTool(
            QuaderEditorApi& editor,
            const ViewportInteractionFrame& frame,
            const ViewportToolResult& toolResult,
            const ViewportBeforeToolInteractionResult& beforeTool);
    [[nodiscard]] bool cancelActiveDrag(QuaderEditorApi& editor);
    void reset();

    [[nodiscard]] const quader_editor_core::TransformGizmoFrame* transformGizmoFrame() const;

private:
    TransformGizmoInteraction transformGizmo_;
    ViewportSelectionInteraction selection_;
};

} // namespace quader

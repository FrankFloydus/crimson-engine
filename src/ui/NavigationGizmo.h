////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportLayout.h"

#include <imgui.h>

namespace quader {

/**
 * Owns Navigation Gizmo behavior within the ImGui editor presentation layer.
 */
class NavigationGizmo final {
public:
    [[nodiscard]] bool visible() const { return visible_; }
    void setVisible(bool visible);

    [[nodiscard]] bool inputLocked() const { return dragging_ && dragPaneActive_; }
    [[nodiscard]] ViewportPaneId lockedPaneId() const { return dragPaneId_; }

    bool draw(ViewportCameraController& camera,
            ViewportPaneId paneId,
            ImVec2 itemMin,
            ImVec2 itemSize,
            Vec2 capturedMouseDelta,
            bool* viewportCursorCaptureRequested,
            Vec2* viewportCursorCapturePoint,
            bool interactive = true);

private:
    bool visible_ = true;
    bool dragging_ = false;
    bool dragPaneActive_ = false;
    ViewportPaneId dragPaneId_ = ViewportPaneId::Perspective;
    bool dragStarted_ = false;
    bool pressedFace_ = false;
    ViewportViewAxis pressedAxis_ = ViewportViewAxis::Z;
    int pressedSign_ = 1;
    Vec2 accumulatedDrag_ = {};
    Vec2 cursorCaptureScreen_ = {};
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ViewportPaneController.h"

namespace quader {

void ViewportPaneController::resetCamera(ViewportPaneId paneId, ViewportCameraController& camera) const {
    resetCamera(viewportViewForPane(paneId), camera);
}

void ViewportPaneController::resetCamera(ViewportView view, ViewportCameraController& camera) const {
    camera = ViewportCameraController{};
    switch (view) {
    case ViewportView::Top:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Y, 1);
        break;
    case ViewportView::Bottom:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Y, -1);
        break;
    case ViewportView::Front:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Z, -1);
        break;
    case ViewportView::Back:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::Z, 1);
        break;
    case ViewportView::Side:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::X, 1);
        break;
    case ViewportView::Left:
        camera.focusOrigin(64.0f);
        camera.viewAxis(ViewportViewAxis::X, -1);
        break;
    case ViewportView::Perspective:
        break;
    }
}

ImVec2 ViewportPaneController::paneRectMin(
        ImVec2 viewportMin,
        ImVec2 viewportSize,
        ViewportNormalizedRect normalizedRect) {
    const ViewportNormalizedRect absolute = viewportAbsolutePaneRect(
            {.x = viewportMin.x, .y = viewportMin.y, .width = viewportSize.x, .height = viewportSize.y},
            normalizedRect);
    return {absolute.x, absolute.y};
}

ImVec2 ViewportPaneController::paneRectSize(ImVec2 viewportSize, ViewportNormalizedRect normalizedRect) {
    const ViewportNormalizedRect absolute = viewportAbsolutePaneRect(
            {.x = 0.0f, .y = 0.0f, .width = viewportSize.x, .height = viewportSize.y},
            normalizedRect);
    return {absolute.width, absolute.height};
}

} // namespace quader

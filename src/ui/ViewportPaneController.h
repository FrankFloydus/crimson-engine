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
 * Coordinates Viewport Pane Controller state transitions for the ImGui editor presentation layer.
 */
class ViewportPaneController {
public:
    void resetCamera(ViewportPaneId paneId, ViewportCameraController& camera) const;
    void resetCamera(ViewportView view, ViewportCameraController& camera) const;

    [[nodiscard]] static ImVec2 paneRectMin(
            ImVec2 viewportMin,
            ImVec2 viewportSize,
            ViewportNormalizedRect normalizedRect);
    [[nodiscard]] static ImVec2 paneRectSize(ImVec2 viewportSize, ViewportNormalizedRect normalizedRect);
};

} // namespace quader

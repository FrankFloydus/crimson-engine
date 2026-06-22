#pragma once

#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportLayout.h"

#include <imgui.h>

namespace quader {

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

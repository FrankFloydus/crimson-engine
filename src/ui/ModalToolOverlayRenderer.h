#pragma once

#include "modeling/ModelingSessionService.h"
#include "viewport/ViewportCameraController.h"

#include <imgui.h>

namespace quader {

class LocalizationService;

class ModalToolOverlayRenderer {
public:
    void drawBoxPreview(
            ImDrawList* drawList,
            const ModelingBoxPreview& preview,
            const ViewportCameraController& camera,
            ImVec2 itemMin,
            ImVec2 itemSize) const;

    void drawFlyNavigationOverlay(
            ImDrawList* drawList,
            ImVec2 itemMin,
            ImVec2 itemSize,
            bool drawCrosshair,
            bool drawSpeed,
            bool drawResetHint,
            float speed,
            const LocalizationService& localization) const;
};

} // namespace quader

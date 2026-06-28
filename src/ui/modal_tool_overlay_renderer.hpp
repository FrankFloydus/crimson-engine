////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <imgui.h>

namespace quader {

class LocalizationService;

/**
 * Owns Modal Tool Overlay Renderer behavior within the ImGui editor presentation layer.
 */
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

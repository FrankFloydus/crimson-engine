////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ModalToolOverlayRenderer.h"

#include "core/LocalizationService.h"
#include "ui/ViewportScreenGeometry.h"

#include <array>
#include <cstdio>
#include <string>

namespace quader {
namespace {

void drawWorldLine(
        ImDrawList* drawList,
        QVec3 a,
        QVec3 b,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize,
        ImU32 color,
        float thickness) {
    const std::optional<ImVec2> screenA = projectWorldToViewport(a, camera, itemMin, itemSize);
    const std::optional<ImVec2> screenB = projectWorldToViewport(b, camera, itemMin, itemSize);
    if (screenA && screenB) {
        drawList->AddLine(*screenA, *screenB, color, thickness);
    }
}

} // namespace

void ModalToolOverlayRenderer::drawBoxPreview(
        ImDrawList* drawList,
        const ModelingBoxPreview& preview,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) const {
    if (!preview.valid) {
        return;
    }

    const ImU32 color = IM_COL32(255, 235, 41, 209);
    if (preview.cornerCount >= 8U) {
        constexpr std::array<std::array<int, 2>, 12> edges = {{
                {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
                {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
                {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
        }};

        for (const auto& edge : edges) {
            drawWorldLine(drawList, preview.corners[edge[0]], preview.corners[edge[1]], camera, itemMin, itemSize, color, 2.0f);
        }
        return;
    }

    if (preview.footprintCount >= 4U) {
        for (std::size_t index = 0; index < preview.footprintCount; ++index) {
            const QVec3 a = preview.footprint[index];
            const QVec3 b = preview.footprint[(index + 1U) % preview.footprintCount];
            drawWorldLine(drawList, a, b, camera, itemMin, itemSize, color, 2.0f);
        }
    }
}

void ModalToolOverlayRenderer::drawFlyNavigationOverlay(
        ImDrawList* drawList,
        ImVec2 itemMin,
        ImVec2 itemSize,
        bool drawCrosshair,
        bool drawSpeed,
        bool drawResetHint,
        float speed,
        const LocalizationService& localization) const {
    const ImVec2 center{itemMin.x + itemSize.x * 0.5f, itemMin.y + itemSize.y * 0.5f};
    if (drawCrosshair) {
        const float gap = 4.0f;
        const float length = 9.0f;
        const ImU32 shadow = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f));
        const ImU32 color = ImGui::GetColorU32(ImVec4(0.94f, 0.96f, 1.0f, 0.88f));
        drawList->AddLine({center.x - length - 1.0f, center.y}, {center.x - gap - 1.0f, center.y}, shadow, 2.0f);
        drawList->AddLine({center.x + gap + 1.0f, center.y}, {center.x + length + 1.0f, center.y}, shadow, 2.0f);
        drawList->AddLine({center.x, center.y - length - 1.0f}, {center.x, center.y - gap - 1.0f}, shadow, 2.0f);
        drawList->AddLine({center.x, center.y + gap + 1.0f}, {center.x, center.y + length + 1.0f}, shadow, 2.0f);
        drawList->AddLine({center.x - length, center.y}, {center.x - gap, center.y}, color, 1.25f);
        drawList->AddLine({center.x + gap, center.y}, {center.x + length, center.y}, color, 1.25f);
        drawList->AddLine({center.x, center.y - length}, {center.x, center.y - gap}, color, 1.25f);
        drawList->AddLine({center.x, center.y + gap}, {center.x, center.y + length}, color, 1.25f);
    }

    if (!drawSpeed) {
        return;
    }

    char speedValue[32]{};
    std::snprintf(speedValue, sizeof(speedValue), "%.1f", speed);
    std::string label;
    if (drawResetHint) {
        label = localization.format(
                "viewport.fly_navigation.speed_with_reset",
                "Speed {speed} (Middle Mouse to reset)",
                {{"speed", speedValue}});
    } else {
        label = localization.format(
                "viewport.fly_navigation.speed",
                "Speed {speed}",
                {{"speed", speedValue}});
    }
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 padding{10.0f, 5.0f};
    const ImVec2 min{
            itemMin.x + itemSize.x * 0.5f - textSize.x * 0.5f - padding.x,
            itemMin.y + itemSize.y - 36.0f,
    };
    const ImVec2 max{min.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f};
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.07f, 0.78f)), 4.0f);
    drawList->AddText({min.x + padding.x, min.y + padding.y}, ImGui::GetColorU32(ImVec4(0.92f, 0.95f, 1.0f, 1.0f)), label.c_str());
}

} // namespace quader

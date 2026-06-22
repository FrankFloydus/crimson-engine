#pragma once

#include <imgui.h>
#include <spdlog/common.h>

#include <filesystem>

namespace quader {

struct UIStyleTokens {
    float fontSize = 18.0f;
    ImVec2 windowPadding = {8.0f, 8.0f};
    ImVec2 framePadding = {7.0f, 4.0f};
    ImVec2 itemSpacing = {8.0f, 6.0f};
    ImGuiWindowFlags panelWindowFlags = ImGuiWindowFlags_NoCollapse;
    float sidebarMinWidth = 340.0f;

    ImVec4 axisX = {0.95f, 0.28f, 0.28f, 1.0f};
    ImVec4 axisY = {0.36f, 0.82f, 0.34f, 1.0f};
    ImVec4 axisZ = {0.33f, 0.58f, 1.0f, 1.0f};

    ImVec4 logTrace = {0.62f, 0.62f, 0.62f, 1.0f};
    ImVec4 logDebug = {0.48f, 0.72f, 1.0f, 1.0f};
    ImVec4 logInfo = {0.86f, 0.88f, 0.90f, 1.0f};
    ImVec4 logWarn = {1.0f, 0.78f, 0.32f, 1.0f};
    ImVec4 logError = {1.0f, 0.32f, 0.32f, 1.0f};
};

void applyUIStyle(const UIStyleTokens& tokens);
void loadUIDefaultFont(const std::filesystem::path& executableDir, const UIStyleTokens& tokens);
ImVec4 uiLogLevelColor(const UIStyleTokens& tokens, spdlog::level::level_enum level);

} // namespace quader

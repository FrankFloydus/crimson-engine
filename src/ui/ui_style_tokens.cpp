////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_style_tokens.hpp"

#include "core/logger.hpp"

#include <cstdio>

namespace quader {
namespace {

ImVec4 styleColor(int red, int green, int blue, float alpha = 1.0f) {
    return {
            static_cast<float>(red) / 255.0f,
            static_cast<float>(green) / 255.0f,
            static_cast<float>(blue) / 255.0f,
            alpha,
    };
}

} // namespace

void applyUIStyle(const UIStyleTokens& tokens) {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = tokens.windowPadding;
    style.FramePadding = tokens.framePadding;
    style.ItemSpacing = tokens.itemSpacing;
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowMenuButtonPosition = ImGuiDir_Left;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = styleColor(216, 216, 216);
    colors[ImGuiCol_TextDisabled] = styleColor(131, 131, 131);
    colors[ImGuiCol_WindowBg] = styleColor(15, 15, 15);
    colors[ImGuiCol_ChildBg] = styleColor(15, 15, 15);
    colors[ImGuiCol_PopupBg] = styleColor(15, 15, 15);
    colors[ImGuiCol_Border] = styleColor(72, 74, 82);
    colors[ImGuiCol_FrameBg] = styleColor(23, 24, 27);
    colors[ImGuiCol_FrameBgHovered] = styleColor(38, 40, 45);
    colors[ImGuiCol_FrameBgActive] = styleColor(50, 54, 61);
    colors[ImGuiCol_TitleBg] = styleColor(25, 25, 25);
    colors[ImGuiCol_TitleBgActive] = styleColor(25, 25, 25);
    colors[ImGuiCol_TitleBgCollapsed] = styleColor(20, 20, 20);
    colors[ImGuiCol_MenuBarBg] = styleColor(25, 25, 25);
    colors[ImGuiCol_Button] = styleColor(25, 25, 25);
    colors[ImGuiCol_ButtonHovered] = styleColor(46, 46, 46);
    colors[ImGuiCol_ButtonActive] = styleColor(60, 60, 60);
    colors[ImGuiCol_Header] = styleColor(25, 25, 25);
    colors[ImGuiCol_HeaderHovered] = styleColor(59, 41, 82);
    colors[ImGuiCol_HeaderActive] = styleColor(36, 23, 52);
    colors[ImGuiCol_Tab] = styleColor(20, 20, 20);
    colors[ImGuiCol_TabHovered] = styleColor(59, 41, 82);
    colors[ImGuiCol_TabSelected] = styleColor(25, 25, 25);
    colors[ImGuiCol_TabSelectedOverline] = styleColor(183, 139, 233);
    colors[ImGuiCol_Separator] = styleColor(40, 40, 40);
    colors[ImGuiCol_SeparatorHovered] = styleColor(183, 139, 233);
    colors[ImGuiCol_SeparatorActive] = styleColor(154, 100, 216);
    colors[ImGuiCol_CheckMark] = styleColor(236, 241, 245);
    colors[ImGuiCol_SliderGrab] = styleColor(183, 139, 233);
    colors[ImGuiCol_SliderGrabActive] = styleColor(154, 100, 216);
    colors[ImGuiCol_ResizeGrip] = styleColor(91, 91, 91, 0.35f);
    colors[ImGuiCol_ResizeGripHovered] = styleColor(183, 139, 233, 0.7f);
    colors[ImGuiCol_ResizeGripActive] = styleColor(154, 100, 216, 0.9f);
    colors[ImGuiCol_DockingPreview] = styleColor(183, 139, 233, 0.35f);
}

void loadUIDefaultFont(const std::filesystem::path& executableDir, const UIStyleTokens& tokens) {
    ImGuiIO& io = ImGui::GetIO();
    const std::filesystem::path fontPath = executableDir / "fonts" / "Inter.ttf";
    ImFontConfig config;
    config.FontDataOwnedByAtlas = true;
    std::snprintf(config.Name, sizeof(config.Name), "Inter");

    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), tokens.fontSize, &config);
    if (font == nullptr) {
        LOG_WARN("Unable to load Inter font from {}; using ImGui default font", fontPath.string());
        io.Fonts->AddFontDefault();
        return;
    }

    io.FontDefault = font;
    LOG_INFO("Loaded Inter as default ImGui font from {}", fontPath.string());
}

ImVec4 uiLogLevelColor(const UIStyleTokens& tokens, spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace:
        return tokens.logTrace;
    case spdlog::level::debug:
        return tokens.logDebug;
    case spdlog::level::warn:
        return tokens.logWarn;
    case spdlog::level::err:
    case spdlog::level::critical:
        return tokens.logError;
    default:
        return tokens.logInfo;
    }
}

} // namespace quader

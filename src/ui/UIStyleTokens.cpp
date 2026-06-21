#include "ui/UIStyleTokens.h"

#include "core/Logger.h"

#include <cstdio>

namespace quader {

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
    style.FrameBorderSize = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_Left;
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

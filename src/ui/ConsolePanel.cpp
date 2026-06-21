#include "ui/ConsolePanel.h"

#include "core/LocalizationService.h"
#include "core/Logger.h"
#include "ui/UIContext.h"
#include "ui/UILayout.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <spdlog/common.h>

#include <array>
#include <string>

namespace quader {
namespace {

const char* levelKey(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace:
        return "ui.console.level.trace";
    case spdlog::level::debug:
        return "ui.console.level.debug";
    case spdlog::level::info:
        return "ui.console.level.info";
    case spdlog::level::warn:
        return "ui.console.level.warn";
    case spdlog::level::err:
        return "ui.console.level.error";
    case spdlog::level::critical:
        return "ui.console.level.critical";
    default:
        return "ui.console.level.off";
    }
}

const char* levelFallback(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace:
        return "Trace";
    case spdlog::level::debug:
        return "Debug";
    case spdlog::level::info:
        return "Info";
    case spdlog::level::warn:
        return "Warn";
    case spdlog::level::err:
        return "Error";
    case spdlog::level::critical:
        return "Critical";
    default:
        return "Off";
    }
}

} // namespace

void ConsolePanel::drawContent(UIContext& context) {
    // UIDockPanel routes hover input through context.requestPanelFocus(id()) before content draws.
    const std::string clearLabel = context.localization.imguiLabel("ui.console.clear", "ui.console.clear", "Clear");
    if (ImGui::Button(clearLabel.c_str())) {
        Logger::clearMemory();
    }
    ImGui::SameLine();
    const std::string autoScrollLabel = context.localization.imguiLabel(
            "ui.console.auto_scroll",
            "ui.console.auto_scroll",
            "Auto-scroll");
    ImGui::Checkbox(autoScrollLabel.c_str(), &autoScroll_);
    ImGui::SameLine();

    const std::array levels = {
        spdlog::level::trace,
        spdlog::level::debug,
        spdlog::level::info,
        spdlog::level::warn,
        spdlog::level::err,
        spdlog::level::critical,
        spdlog::level::off,
    };

    const std::string preview = context.localization.text(levelKey(filter_.minimumLevel), levelFallback(filter_.minimumLevel));
    ImGui::SetNextItemWidth(120.0f);
    const std::string levelLabel = context.localization.imguiLabel("ui.console.level", "ui.console.level", "Level");
    if (ImGui::BeginCombo(levelLabel.c_str(), preview.c_str())) {
        for (const spdlog::level::level_enum level : levels) {
            const bool selected = level == filter_.minimumLevel;
            std::string stableId = "ui.console.level.";
            stableId.append(levelFallback(level));
            const std::string itemLabel = context.localization.imguiLabel(levelKey(level), stableId, levelFallback(level));
            if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                filter_.minimumLevel = level;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    setNextItemFillWidthAtLeast(120.0f);
    const std::string searchHint = context.localization.text("ui.console.filter_logs", "Filter logs");
    ImGui::InputTextWithHint("##ConsoleSearch", searchHint.c_str(), &filter_.searchText);
    ImGui::Separator();

    const auto filtered = ConsoleModel::filter(Logger::entriesSnapshot(), filter_);
    if (ImGui::BeginChild("ConsoleScroll", {0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const LogEntry& entry = filtered[static_cast<std::size_t>(row)];
                ImGui::PushStyleColor(ImGuiCol_Text, uiLogLevelColor(context.style, entry.level));
                ImGui::TextUnformatted(entry.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

} // namespace quader

#include "ui/ConsolePanel.h"

#include "core/Logger.h"
#include "ui/UIContext.h"
#include "ui/UILayout.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <spdlog/common.h>

#include <array>

namespace quader {
namespace {

const char* levelName(spdlog::level::level_enum level) {
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
    if (ImGui::Button("Clear")) {
        Logger::clearMemory();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
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

    const char* preview = levelName(filter_.minimumLevel);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("Level", preview)) {
        for (const spdlog::level::level_enum level : levels) {
            const bool selected = level == filter_.minimumLevel;
            if (ImGui::Selectable(levelName(level), selected)) {
                filter_.minimumLevel = level;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    setNextItemFillWidthAtLeast(120.0f);
    ImGui::InputTextWithHint("##ConsoleSearch", "Filter logs", &filter_.searchText);
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

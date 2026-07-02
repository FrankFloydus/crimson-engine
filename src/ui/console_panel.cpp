////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/console_panel.hpp"

#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "ui/imgui_quader_widgets.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_layout.hpp"
#include "ui/ui_style_tokens.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <spdlog/common.h>

#include <array>
#include <string>

namespace quader {
namespace {

const char *level_key(spdlog::level::level_enum level) {
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

const char *level_fallback(spdlog::level::level_enum level) {
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

void ConsolePanel::draw_content(UIContext& context) {
    // UIDockPanel routes hover input through context.requestPanelFocus(id()) before content draws.
    const std::string clear_label = context.localization.imgui_label(
        "ui.console.clear", "ui.console.clear", "Clear");
    if (ImGui::Button(clear_label.c_str())) {
      Logger::clear_memory();
    }
    ImGui::SameLine();
    const std::string auto_scroll_label = context.localization.imgui_label(
        "ui.console.auto_scroll", "ui.console.auto_scroll", "Auto-scroll");
    ImGui::Checkbox(auto_scroll_label.c_str(), &auto_scroll_);
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

    const std::string preview =
        context.localization.text(level_key(filter_.minimum_level),
                                  level_fallback(filter_.minimum_level));
    ImGui::SetNextItemWidth(120.0f);
    const std::string level_label = context.localization.imgui_label(
        "ui.console.level", "ui.console.level", "Level");
    if (quader_begin_combo(level_label.c_str(), preview.c_str())) {
      for (const spdlog::level::level_enum level : levels) {
        const bool selected = level == filter_.minimum_level;
        std::string stable_id = "ui.console.level.";
        stable_id.append(level_fallback(level));
        const std::string item_label = context.localization.imgui_label(
            level_key(level), stable_id, level_fallback(level));
        if (ImGui::Selectable(item_label.c_str(), selected)) {
          filter_.minimum_level = level;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    set_next_item_fill_width_at_least(120.0f);
    const std::string search_hint =
        context.localization.text("ui.console.filter_logs", "Filter logs");
    ImGui::InputTextWithHint("##ConsoleSearch", search_hint.c_str(),
                             &filter_.search_text);
    ImGui::Separator();

    const auto filtered =
        ConsoleModel::filter(Logger::entries_snapshot(), filter_);
    if (ImGui::BeginChild("ConsoleScroll", {0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const LogEntry& entry = filtered[static_cast<std::size_t>(row)];
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ui_log_level_color(context.style, entry.level));
                ImGui::TextUnformatted(entry.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
          ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

} // namespace quader

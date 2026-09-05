////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_settings.hpp"

#include "core/qstr.hpp"
#include "core/user_config_paths.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

bool parse_bool(std::string value, bool fallback) {
  const std::optional<bool> parsed = QStr::parse_bool_token(value);
  return parsed.value_or(fallback);
}

int parse_bounded_integer(std::string_view value, int minimum, int maximum,
                          int fallback) {
  int parsed = 0;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} ||
      result.ptr != value.data() + value.size() || parsed < minimum ||
      parsed > maximum) {
    return fallback;
  }
  return parsed;
}

std::optional<UIPanelId> panel_settings_id(std::string_view key) {
  for (UIPanelId id : kUiDockPanelIds) {
    if (key == ui_panel_stable_id(id) || key == ui_panel_key(id)) {
      return id;
    }
  }
  return std::nullopt;
}

} // namespace

UISettings UISettings::defaults() {
    return {};
}

UISettings UISettings::load(const std::filesystem::path& path) {
    UISettings settings = defaults();
    std::ifstream file(path);
    if (!file.is_open()) {
        return settings;
    }

    std::string line;
    while (std::getline(file, line)) {
      line = QStr::trim_ascii_whitespace(line);
      if (line.empty() || line.front() == '#' || line.front() == '[') {
        continue;
      }

      const std::optional<QStrSplitOnce> split = QStr::split_once(line, '=');
      if (!split) {
        continue;
      }

      const std::string key = QStr::trim_ascii_whitespace(split->before);
      const std::string value = QStr::trim_ascii_whitespace(split->after);
      if (const std::optional<UIPanelId> panel_id = panel_settings_id(key)) {
        settings.set_panel_visible(
            *panel_id,
            parse_bool(value, settings.is_panel_visible(*panel_id)));
      } else if (key == "NavigationGizmo") {
        settings.navigation_gizmo_visible_ =
            parse_bool(value, settings.navigation_gizmo_visible_);
      } else if (key == "Locale") {
        settings.set_locale(value);
      } else if (key == "VSync") {
        settings.set_vsync_enabled(parse_bool(value, settings.vsync_enabled()));
      } else if (key == "UndoSteps") {
        settings.session_settings_.undo_steps = parse_bounded_integer(
            value, SessionPreferences::kMinimumUndoSteps,
            SessionPreferences::kMaximumUndoSteps,
            settings.session_settings_.undo_steps);
      } else if (key == "AutoSaveEnabled") {
        settings.session_settings_.autosave_enabled =
            parse_bool(value, settings.session_settings_.autosave_enabled);
      } else if (key == "AutoSaveIntervalMinutes") {
        settings.session_settings_.autosave_interval_minutes =
            parse_bounded_integer(
                value, SessionPreferences::kMinimumAutoSaveIntervalMinutes,
                SessionPreferences::kMaximumAutoSaveIntervalMinutes,
                settings.session_settings_.autosave_interval_minutes);
      } else if (key == "AutoSaveMaxFiles") {
        settings.session_settings_.autosave_max_files = parse_bounded_integer(
            value, SessionPreferences::kMinimumAutoSaveMaxFiles,
            SessionPreferences::kMaximumAutoSaveMaxFiles,
            settings.session_settings_.autosave_max_files);
      } else if (key == "InterfaceScale") {
        if (const std::optional<float> parsed =
                QStr::parse_finite_float_token(value)) {
          settings.set_interface_scale(*parsed);
        }
      }
    }

    return settings;
}

UISettingsPaths
UISettings::resolve_paths(const std::filesystem::path &executable_dir) {
  UISettingsPaths paths;
  paths.directory = quader_user_config_directory(executable_dir);
  paths.qt_window_state_file = paths.directory / "qt_window_state.bin";
  paths.qt_auxiliary_window_state_file =
      paths.directory / "qt_auxiliary_windows.ini";
  paths.settings_file = paths.directory / "ui_settings.ini";
  paths.viewport_settings_file = paths.directory / "viewport_settings.ini";
  return paths;
}

bool UISettings::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "[Panels]\n";
    for (UIPanelId id : kUiDockPanelIds) {
      file << ui_panel_stable_id(id) << '='
           << (is_panel_visible(id) ? "true" : "false") << '\n';
    }
    file << "\n[Show]\n";
    file << "NavigationGizmo=" << (navigation_gizmo_visible_ ? "true" : "false")
         << '\n';
    file << "\n[Localization]\n";
    file << "Locale=" << locale_ << '\n';
    file << "\n[Interface]\n";
    file << std::fixed << std::setprecision(6);
    file << "InterfaceScale=" << interface_scale_ << '\n';
    file << "\n[Rendering]\n";
    file << "VSync=" << (vsync_enabled_ ? "true" : "false") << '\n';
    file << "\n[History]\n";
    file << "UndoSteps=" << session_settings_.undo_steps << '\n';
    file << "\n[AutoSave]\n";
    file << "AutoSaveEnabled="
         << (session_settings_.autosave_enabled ? "true" : "false") << '\n';
    file << "AutoSaveIntervalMinutes="
         << session_settings_.autosave_interval_minutes << '\n';
    file << "AutoSaveMaxFiles=" << session_settings_.autosave_max_files << '\n';
    file.flush();
    return file.good();
}

bool UISettings::is_panel_visible(UIPanelId id) const {
  switch (id) {
  case UIPanelId::Viewport:
    return true;
  case UIPanelId::Outliner:
    return outliner_visible_;
  case UIPanelId::Inspector:
    return inspector_visible_;
  case UIPanelId::Console:
    return console_visible_;
  case UIPanelId::Layers:
    return layers_visible_;
  case UIPanelId::ToolSettings:
    return tool_settings_visible_;
  case UIPanelId::ActiveMaterial:
    return active_material_visible_;
  case UIPanelId::Count:
    break;
  }
  return true;
}

void UISettings::set_panel_visible(UIPanelId id, bool visible) {
  switch (id) {
  case UIPanelId::Viewport:
    break;
  case UIPanelId::Outliner:
    outliner_visible_ = visible;
    break;
  case UIPanelId::Inspector:
    inspector_visible_ = visible;
    break;
  case UIPanelId::Console:
    console_visible_ = visible;
    break;
  case UIPanelId::Layers:
    layers_visible_ = visible;
    break;
  case UIPanelId::ToolSettings:
    tool_settings_visible_ = visible;
    break;
  case UIPanelId::ActiveMaterial:
    active_material_visible_ = visible;
    break;
  case UIPanelId::Count:
    break;
  }
}

void UISettings::set_locale(std::string locale) {
  locale = QStr::trim_ascii_whitespace(locale);
  if (!locale.empty()) {
    locale_ = std::move(locale);
  }
}

void UISettings::set_interface_scale(float scale) {
  if (!std::isfinite(scale)) {
    interface_scale_ = kDefaultInterfaceScale;
    return;
  }
  interface_scale_ =
      std::clamp(scale, kMinimumInterfaceScale, kMaximumInterfaceScale);
}

bool UISettings::set_session_settings(SessionPreferences settings) {
  if (!settings.valid()) {
    return false;
  }
  session_settings_ = settings;
  return true;
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_settings.hpp"

#include "core/qstr.hpp"
#include "core/user_config_paths.hpp"

#include <fstream>
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

bool panel_settings_key_matches(UIPanelId id, std::string_view key) {
  return key == ui_panel_stable_id(id) || key == ui_panel_key(id);
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
      if (panel_settings_key_matches(UIPanelId::Outliner, key)) {
        settings.outliner_visible_ =
            parse_bool(value, settings.outliner_visible_);
      } else if (panel_settings_key_matches(UIPanelId::Inspector, key)) {
        settings.inspector_visible_ =
            parse_bool(value, settings.inspector_visible_);
      } else if (panel_settings_key_matches(UIPanelId::Console, key)) {
        settings.console_visible_ =
            parse_bool(value, settings.console_visible_);
      } else if (panel_settings_key_matches(UIPanelId::Layers, key)) {
        settings.layers_visible_ =
            parse_bool(value, settings.layers_visible_);
      } else if (key == "NavigationGizmo") {
        settings.navigation_gizmo_visible_ =
            parse_bool(value, settings.navigation_gizmo_visible_);
      } else if (key == "Locale") {
        settings.set_locale(value);
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
    file << ui_panel_stable_id(UIPanelId::Outliner) << '='
         << (outliner_visible_ ? "true" : "false") << '\n';
    file << ui_panel_stable_id(UIPanelId::Inspector) << '='
         << (inspector_visible_ ? "true" : "false") << '\n';
    file << ui_panel_stable_id(UIPanelId::Console) << '='
         << (console_visible_ ? "true" : "false") << '\n';
    file << ui_panel_stable_id(UIPanelId::Layers) << '='
         << (layers_visible_ ? "true" : "false") << '\n';
    file << "\n[Show]\n";
    file << "NavigationGizmo=" << (navigation_gizmo_visible_ ? "true" : "false")
         << '\n';
    file << "\n[Localization]\n";
    file << "Locale=" << locale_ << '\n';
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
  }
}

void UISettings::set_locale(std::string locale) {
  locale = QStr::trim_ascii_whitespace(locale);
  if (!locale.empty()) {
    locale_ = std::move(locale);
  }
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/UISettings.h"

#include "core/QStr.h"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

bool parseBool(std::string value, bool fallback) {
    const std::optional<bool> parsed = QStr::parseBoolToken(value);
    return parsed.value_or(fallback);
}

std::filesystem::path localAppDataDirectory(const std::filesystem::path& executableDir) {
#ifdef _WIN32
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        if (localAppData[0] != '\0') {
            return std::filesystem::path(localAppData) / "Quader";
        }
    }
#else
    if (const char* configHome = std::getenv("XDG_CONFIG_HOME")) {
        if (configHome[0] != '\0') {
            return std::filesystem::path(configHome) / "Quader";
        }
    }
#endif
    return executableDir;
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
        line = QStr::trimAsciiWhitespace(line);
        if (line.empty() || line.front() == '#' || line.front() == '[') {
            continue;
        }

        const std::optional<QStrSplitOnce> split = QStr::splitOnce(line, '=');
        if (!split) {
            continue;
        }

        const std::string key = QStr::trimAsciiWhitespace(split->before);
        const std::string value = QStr::trimAsciiWhitespace(split->after);
        if (key == uiPanelKey(UIPanelId::Outliner)) {
            settings.outlinerVisible_ = parseBool(value, settings.outlinerVisible_);
        } else if (key == uiPanelKey(UIPanelId::Inspector)) {
            settings.inspectorVisible_ = parseBool(value, settings.inspectorVisible_);
        } else if (key == uiPanelKey(UIPanelId::Console)) {
            settings.consoleVisible_ = parseBool(value, settings.consoleVisible_);
        } else if (key == "NavigationGizmo") {
            settings.navigationGizmoVisible_ = parseBool(value, settings.navigationGizmoVisible_);
        } else if (key == "Locale") {
            settings.setLocale(value);
        }
    }

    return settings;
}

UISettingsPaths UISettings::resolvePaths(const std::filesystem::path& executableDir) {
    UISettingsPaths paths;
    paths.directory = localAppDataDirectory(executableDir);
    paths.imguiIniFile = paths.directory / "quader_ui.ini";
    paths.settingsFile = paths.directory / "ui_settings.ini";
    paths.viewportSettingsFile = paths.directory / "viewport_settings.ini";
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
    file << uiPanelKey(UIPanelId::Outliner) << '=' << (outlinerVisible_ ? "true" : "false") << '\n';
    file << uiPanelKey(UIPanelId::Inspector) << '=' << (inspectorVisible_ ? "true" : "false") << '\n';
    file << uiPanelKey(UIPanelId::Console) << '=' << (consoleVisible_ ? "true" : "false") << '\n';
    file << "\n[Show]\n";
    file << "NavigationGizmo=" << (navigationGizmoVisible_ ? "true" : "false") << '\n';
    file << "\n[Localization]\n";
    file << "Locale=" << locale_ << '\n';
    return true;
}

bool UISettings::isPanelVisible(UIPanelId id) const {
    switch (id) {
    case UIPanelId::Viewport:
        return true;
    case UIPanelId::Outliner:
        return outlinerVisible_;
    case UIPanelId::Inspector:
        return inspectorVisible_;
    case UIPanelId::Console:
        return consoleVisible_;
    }
    return true;
}

void UISettings::setPanelVisible(UIPanelId id, bool visible) {
    switch (id) {
    case UIPanelId::Viewport:
        break;
    case UIPanelId::Outliner:
        outlinerVisible_ = visible;
        break;
    case UIPanelId::Inspector:
        inspectorVisible_ = visible;
        break;
    case UIPanelId::Console:
        consoleVisible_ = visible;
        break;
    }
}

void UISettings::setLocale(std::string locale) {
    locale = QStr::trimAsciiWhitespace(locale);
    if (!locale.empty()) {
        locale_ = std::move(locale);
    }
}

} // namespace quader

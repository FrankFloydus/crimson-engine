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
#include <string_view>
#include <utility>

namespace quader {
namespace {

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

bool parseBool(std::string_view value, bool fallback) {
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    return fallback;
}

int parseInt(std::string_view value, int fallback) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(std::string(value), &consumed);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
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

    std::optional<DockLocation> outlinerDockLocation;
    std::optional<DockLocation> inspectorDockLocation;
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
        if (key == "Locale") {
            settings.setLocale(value);
        } else if (key == "SidebarSide") {
            if (const std::optional<MainLayoutSidebarSide> side = mainLayoutSidebarSideFromName(value)) {
                settings.mainLayout().sidebarSide = *side;
            }
        } else if (key == "SidebarWidthDp") {
            settings.mainLayout().sidebarWidthDp =
                    mainLayoutClampedSidebarWidthDp(parseInt(value, settings.mainLayout().sidebarWidthDp));
        } else if (key == "SidebarCollapsed") {
            settings.mainLayout().sidebarCollapsed = parseBool(value, settings.mainLayout().sidebarCollapsed);
        } else if (key == "OutlinerHeightDp") {
            settings.mainLayout().outlinerHeightDp =
                    mainLayoutClampedOutlinerHeightDp(parseInt(value, settings.mainLayout().outlinerHeightDp));
        } else if (key == "BottomDockHeightDp") {
            settings.mainLayout().bottomDockHeightDp =
                    mainLayoutClampedBottomDockHeightDp(parseInt(value, settings.mainLayout().bottomDockHeightDp));
        } else if (key == "OutlinerVisible") {
            settings.mainLayout().outlinerVisible = parseBool(value, settings.mainLayout().outlinerVisible);
        } else if (key == "InspectorVisible") {
            settings.mainLayout().inspectorVisible = parseBool(value, settings.mainLayout().inspectorVisible);
        } else if (key == "OutlinerDockLocation") {
            if (const std::optional<DockLocation> location = dockLocationFromName(value)) {
                outlinerDockLocation = *location;
            }
        } else if (key == "InspectorDockLocation") {
            if (const std::optional<DockLocation> location = dockLocationFromName(value)) {
                inspectorDockLocation = *location;
            }
        }
    }

    settings.setMainLayout(settings.mainLayout());
    if (outlinerDockLocation) {
        const bool changed =
                dockMovePanelToLocation(settings.mainLayout().dockLayout, kMainLayoutPanelOutliner, *outlinerDockLocation);
        (void)changed;
    }
    if (inspectorDockLocation) {
        const bool changed =
                dockMovePanelToLocation(settings.mainLayout().dockLayout, kMainLayoutPanelInspector, *inspectorDockLocation);
        (void)changed;
    }
    return settings;
}

UISettingsPaths UISettings::resolvePaths(const std::filesystem::path& executableDir) {
    UISettingsPaths paths;
    paths.directory = localAppDataDirectory(executableDir);
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

    file << "[Localization]\n";
    file << "Locale=" << locale_ << '\n';
    file << "\n[MainLayout]\n";
    file << "SidebarSide=" << mainLayoutSidebarSideName(mainLayout_.sidebarSide) << '\n';
    file << "SidebarWidthDp=" << mainLayout_.sidebarWidthDp << '\n';
    file << "SidebarCollapsed=" << (mainLayout_.sidebarCollapsed ? "true" : "false") << '\n';
    file << "OutlinerHeightDp=" << mainLayout_.outlinerHeightDp << '\n';
    file << "BottomDockHeightDp=" << mainLayout_.bottomDockHeightDp << '\n';
    file << "OutlinerVisible=" << (mainLayout_.outlinerVisible ? "true" : "false") << '\n';
    file << "InspectorVisible=" << (mainLayout_.inspectorVisible ? "true" : "false") << '\n';
    file << "OutlinerDockLocation="
         << dockLocationName(dockPanelLocation(mainLayout_.dockLayout, kMainLayoutPanelOutliner).value_or(DockLocation::Sidebar))
         << '\n';
    file << "InspectorDockLocation="
         << dockLocationName(dockPanelLocation(mainLayout_.dockLayout, kMainLayoutPanelInspector).value_or(DockLocation::Sidebar))
         << '\n';
    return true;
}

void UISettings::setLocale(std::string locale) {
    locale = QStr::trimAsciiWhitespace(locale);
    if (!locale.empty()) {
        locale_ = std::move(locale);
    }
}

void UISettings::setMainLayout(MainLayoutModel layout) {
    mainLayout_ = normalizedMainLayoutModel(std::move(layout));
}

} // namespace quader

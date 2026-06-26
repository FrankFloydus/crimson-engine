////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/UISettings.h"

#include "core/QStr.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

std::vector<std::string> parseDockPanelOrder(std::string_view value) {
    std::vector<std::string> panelIds;
    while (!value.empty()) {
        std::string_view token = value;
        if (const std::optional<QStrSplitOnce> split = QStr::splitOnce(value, ',')) {
            token = split->before;
            value = split->after;
        } else {
            value = {};
        }

        const std::string trimmed = QStr::trimAsciiWhitespace(token);
        const std::optional<std::string_view> canonical = mainLayoutPanelIdFromLegacySettings(trimmed);
        if (canonical && *canonical != kMainLayoutPanelViewport) {
            panelIds.emplace_back(*canonical);
        }
    }
    return panelIds;
}

std::vector<DockLocationActivePanel> parseDockActivePanels(std::string_view value) {
    std::vector<DockLocationActivePanel> activePanels;
    while (!value.empty()) {
        std::string_view token = value;
        if (const std::optional<QStrSplitOnce> split = QStr::splitOnce(value, ',')) {
            token = split->before;
            value = split->after;
        } else {
            value = {};
        }

        const std::string trimmed = QStr::trimAsciiWhitespace(token);
        const std::optional<QStrSplitOnce> activeSplit = QStr::splitOnce(trimmed, ':');
        if (!activeSplit) {
            continue;
        }

        const std::string locationName = QStr::trimAsciiWhitespace(activeSplit->before);
        const std::string panelId = QStr::trimAsciiWhitespace(activeSplit->after);
        const std::optional<DockLocation> location = dockLocationFromName(locationName);
        const std::optional<std::string_view> canonical = mainLayoutPanelIdFromLegacySettings(panelId);
        if (location && canonical && *canonical != kMainLayoutPanelViewport) {
            activePanels.push_back({
                    .location = *location,
                    .panelId = std::string(*canonical),
            });
        }
    }
    return activePanels;
}

/// Carries persisted split panel ids and optional pane sizes parsed from one dock location entry.
struct ParsedDockSplitPanelList {
    std::vector<std::string> panelIds;
    std::vector<int> panelSizesDp;
};

ParsedDockSplitPanelList parseDockSplitPanelList(std::string_view value) {
    ParsedDockSplitPanelList panels;
    while (!value.empty()) {
        std::string_view token = value;
        if (const std::optional<QStrSplitOnce> split = QStr::splitOnce(value, '|')) {
            token = split->before;
            value = split->after;
        } else {
            value = {};
        }

        std::string panelId = QStr::trimAsciiWhitespace(token);
        int sizeDp = 0;
        if (const std::optional<QStrSplitOnce> sizeSplit = QStr::splitOnce(panelId, '@')) {
            const std::string sizeText = QStr::trimAsciiWhitespace(sizeSplit->after);
            panelId = QStr::trimAsciiWhitespace(sizeSplit->before);
            sizeDp = std::max(0, parseInt(sizeText, 0));
        }

        const std::optional<std::string_view> canonical = mainLayoutPanelIdFromLegacySettings(panelId);
        if (canonical && *canonical != kMainLayoutPanelViewport) {
            panels.panelIds.emplace_back(*canonical);
            panels.panelSizesDp.push_back(sizeDp);
        }
    }
    return panels;
}

std::vector<DockLocationSplitPanels> parseDockSplitPanels(std::string_view value) {
    std::vector<DockLocationSplitPanels> splitPanels;
    while (!value.empty()) {
        std::string_view token = value;
        if (const std::optional<QStrSplitOnce> split = QStr::splitOnce(value, ',')) {
            token = split->before;
            value = split->after;
        } else {
            value = {};
        }

        const std::string trimmed = QStr::trimAsciiWhitespace(token);
        const std::optional<QStrSplitOnce> split = QStr::splitOnce(trimmed, ':');
        if (!split) {
            continue;
        }

        const std::string locationName = QStr::trimAsciiWhitespace(split->before);
        const std::optional<DockLocation> location = dockLocationFromName(locationName);
        if (!location) {
            continue;
        }

        std::string splitPanelList = QStr::trimAsciiWhitespace(split->after);
        std::string tabPanelId;
        if (const std::optional<QStrSplitOnce> tabSplit = QStr::splitOnce(splitPanelList, '=')) {
            tabPanelId = QStr::trimAsciiWhitespace(tabSplit->before);
            splitPanelList = QStr::trimAsciiWhitespace(tabSplit->after);
        }

        ParsedDockSplitPanelList panels = parseDockSplitPanelList(splitPanelList);
        if (!tabPanelId.empty()) {
            const std::optional<std::string_view> canonicalTab = mainLayoutPanelIdFromLegacySettings(tabPanelId);
            tabPanelId = canonicalTab && *canonicalTab != kMainLayoutPanelViewport ?
                    std::string(*canonicalTab) :
                    std::string{};
        }
        if (!panels.panelIds.empty()) {
            if (tabPanelId.empty()) {
                tabPanelId = panels.panelIds.front();
            }
            splitPanels.push_back({
                    .location = *location,
                    .tabPanelId = std::move(tabPanelId),
                    .panelIds = std::move(panels.panelIds),
                    .panelSizesDp = std::move(panels.panelSizesDp),
            });
        }
    }
    return splitPanels;
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

    std::vector<DockPanelPlacement> dockPanelLocations;
    std::vector<std::string> dockPanelOrder;
    std::vector<DockLocationActivePanel> dockActivePanels;
    std::vector<DockLocationSplitPanels> dockSplitPanels;
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
            settings.mainLayout().sidebar.widthDp =
                    mainLayoutClampedSidebarWidthDp(parseInt(value, settings.mainLayout().sidebar.widthDp));
        } else if (key == "AuxiliarySidebarWidthDp") {
            settings.mainLayout().auxiliarySidebar.widthDp =
                    mainLayoutClampedSidebarWidthDp(parseInt(value, settings.mainLayout().auxiliarySidebar.widthDp));
        } else if (key == "SidebarCollapsed") {
            settings.mainLayout().sidebar.collapsed = parseBool(value, settings.mainLayout().sidebar.collapsed);
        } else if (key == "AuxiliarySidebarCollapsed") {
            settings.mainLayout().auxiliarySidebar.collapsed =
                    parseBool(value, settings.mainLayout().auxiliarySidebar.collapsed);
        } else if (key == "OutlinerHeightDp") {
            settings.mainLayout().outlinerHeightDp =
                    mainLayoutClampedOutlinerHeightDp(parseInt(value, settings.mainLayout().outlinerHeightDp));
        } else if (key == "BottomDockHeightDp") {
            settings.mainLayout().bottomDock.heightDp =
                    mainLayoutClampedBottomDockHeightDp(parseInt(value, settings.mainLayout().bottomDock.heightDp));
        } else if (key == "BottomDockCollapsed") {
            settings.mainLayout().bottomDock.collapsed = parseBool(value, settings.mainLayout().bottomDock.collapsed);
        } else if (const std::optional<std::string_view> panelId =
                           mainLayoutPanelIdFromVisibilitySettingsKey(key)) {
            (void)mainLayoutSetPanelVisible(
                    settings.mainLayout(),
                    *panelId,
                    parseBool(value, mainLayoutPanelVisible(settings.mainLayout(), *panelId)));
        } else if (const std::optional<std::string_view> panelId =
                           mainLayoutPanelIdFromDockLocationSettingsKey(key)) {
            if (const std::optional<DockLocation> location = dockLocationFromName(value)) {
                dockPanelLocations.push_back({
                        .panelId = std::string(*panelId),
                        .location = *location,
                });
            }
        } else if (key == "DockPanelOrder") {
            dockPanelOrder = parseDockPanelOrder(value);
        } else if (key == "DockActivePanels") {
            dockActivePanels = parseDockActivePanels(value);
        } else if (key == "DockSplitPanels") {
            dockSplitPanels = parseDockSplitPanels(value);
        }
    }

    settings.setMainLayout(settings.mainLayout());
    for (const DockPanelPlacement& placement : dockPanelLocations) {
        const bool changed =
                dockMovePanelToLocation(settings.mainLayout().dockLayout, placement.panelId, placement.location);
        (void)changed;
    }
    if (!dockPanelOrder.empty()) {
        std::vector<std::string_view> panelOrderViews;
        panelOrderViews.reserve(dockPanelOrder.size());
        for (const std::string& panelId : dockPanelOrder) {
            panelOrderViews.push_back(panelId);
        }
        const bool changed = dockReorderPanels(settings.mainLayout().dockLayout, panelOrderViews);
        (void)changed;
    }
    for (const DockLocationActivePanel& activePanel : dockActivePanels) {
        const bool changed = dockSetActivePanel(
                settings.mainLayout().dockLayout,
                activePanel.location,
                activePanel.panelId);
        (void)changed;
    }
    for (const DockLocationSplitPanels& splitPanels : dockSplitPanels) {
        std::vector<std::string_view> panelIds;
        panelIds.reserve(splitPanels.panelIds.size());
        for (const std::string& panelId : splitPanels.panelIds) {
            panelIds.push_back(panelId);
        }
        const bool changed = dockSetSplitPanels(
                settings.mainLayout().dockLayout,
                splitPanels.location,
                panelIds,
                splitPanels.tabPanelId);
        (void)changed;
        for (std::size_t index = 0; index < splitPanels.panelIds.size(); ++index) {
            if (index >= splitPanels.panelSizesDp.size() || splitPanels.panelSizesDp[index] <= 0) {
                continue;
            }
            int sizeDp = splitPanels.panelSizesDp[index];
            if (splitPanels.location == DockLocation::Panel) {
                sizeDp = mainLayoutClampedBottomSplitPanelWidthDp(sizeDp);
            } else if (mainLayoutLocationIsSideDock(splitPanels.location)) {
                sizeDp = mainLayoutClampedSidePanelStackHeightDp(sizeDp);
            }
            const bool resized = dockSetSplitPanelSizeDp(
                    settings.mainLayout().dockLayout,
                    splitPanels.location,
                    splitPanels.panelIds[index],
                    sizeDp);
            (void)resized;
        }
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
    file << "SidebarWidthDp=" << mainLayout_.sidebar.widthDp << '\n';
    file << "AuxiliarySidebarWidthDp=" << mainLayout_.auxiliarySidebar.widthDp << '\n';
    file << "SidebarCollapsed=" << (mainLayout_.sidebar.collapsed ? "true" : "false") << '\n';
    file << "AuxiliarySidebarCollapsed=" << (mainLayout_.auxiliarySidebar.collapsed ? "true" : "false") << '\n';
    file << "OutlinerHeightDp=" << mainLayout_.outlinerHeightDp << '\n';
    file << "BottomDockHeightDp=" << mainLayout_.bottomDock.heightDp << '\n';
    file << "BottomDockCollapsed=" << (mainLayout_.bottomDock.collapsed ? "true" : "false") << '\n';
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (!descriptor.visibilitySettingsKey.empty()) {
            file << descriptor.visibilitySettingsKey << '='
                 << (mainLayoutPanelVisible(mainLayout_, descriptor.id) ? "true" : "false")
                 << '\n';
        }
    }
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (!descriptor.dockLocationSettingsKey.empty()) {
            file << descriptor.dockLocationSettingsKey << '='
                 << dockLocationName(dockPanelLocation(mainLayout_.dockLayout, descriptor.id).value_or(DockLocation::Sidebar))
                 << '\n';
        }
    }
    file << "DockPanelOrder=";
    for (std::size_t index = 0; index < mainLayout_.dockLayout.panels.size(); ++index) {
        if (index > 0) {
            file << ',';
        }
        file << mainLayout_.dockLayout.panels[index].panelId;
    }
    file << '\n';
    file << "DockActivePanels=";
    for (std::size_t index = 0; index < mainLayout_.dockLayout.activePanels.size(); ++index) {
        if (index > 0) {
            file << ',';
        }
        file << dockLocationName(mainLayout_.dockLayout.activePanels[index].location)
             << ':'
             << mainLayout_.dockLayout.activePanels[index].panelId;
    }
    file << '\n';
    file << "DockSplitPanels=";
    for (std::size_t index = 0; index < mainLayout_.dockLayout.splitPanels.size(); ++index) {
        if (index > 0) {
            file << ',';
        }
        const DockLocationSplitPanels& splitPanels = mainLayout_.dockLayout.splitPanels[index];
        file << dockLocationName(splitPanels.location) << ':';
        if (!splitPanels.tabPanelId.empty()) {
            file << splitPanels.tabPanelId << '=';
        }
        for (std::size_t panelIndex = 0; panelIndex < splitPanels.panelIds.size(); ++panelIndex) {
            if (panelIndex > 0) {
                file << '|';
            }
            file << splitPanels.panelIds[panelIndex];
            if (panelIndex < splitPanels.panelSizesDp.size() &&
                    splitPanels.panelSizesDp[panelIndex] > 0) {
                file << '@' << splitPanels.panelSizesDp[panelIndex];
            }
        }
    }
    file << '\n';
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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/organisms/main_layout_model.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace quader {
namespace {

constexpr MainLayoutPanelDescriptorList kMainLayoutPanelDescriptors{{
        {
                .id = kMainLayoutPanelViewport,
                .legacySettingsId = "Viewport",
                .localeKey = "ui.panel.viewport",
                .fallbackLabel = "Viewport",
                .dockable = false,
        },
        {
                .id = kMainLayoutPanelOutliner,
                .legacySettingsId = "Outliner",
                .localeKey = "ui.panel.outliner",
                .fallbackLabel = "Outliner",
                .visibilitySettingsKey = "OutlinerVisible",
                .dockLocationSettingsKey = "OutlinerDockLocation",
                .dockable = true,
        },
        {
                .id = kMainLayoutPanelInspector,
                .legacySettingsId = "Inspector",
                .localeKey = "ui.panel.inspector",
                .fallbackLabel = "Inspector",
                .visibilitySettingsKey = "InspectorVisible",
                .dockLocationSettingsKey = "InspectorDockLocation",
                .dockable = true,
        },
}};

bool panelStateIdIsPresent(const std::vector<MainLayoutPanelState>& states, std::string_view panelId) {
    return std::find_if(states.begin(), states.end(), [panelId](const MainLayoutPanelState& state) {
        return state.panelId == panelId;
    }) != states.end();
}

std::vector<MainLayoutPanelState> normalizedMainLayoutPanelStates(std::vector<MainLayoutPanelState> states) {
    std::vector<MainLayoutPanelState> normalized;
    normalized.reserve(kMainLayoutPanelDescriptors.size());
    for (const MainLayoutPanelDescriptor& descriptor : kMainLayoutPanelDescriptors) {
        if (!descriptor.dockable || panelStateIdIsPresent(normalized, descriptor.id)) {
            continue;
        }
        const auto existing = std::find_if(states.begin(), states.end(), [&descriptor](const MainLayoutPanelState& state) {
            return state.panelId == descriptor.id;
        });
        normalized.push_back({
                .panelId = std::string(descriptor.id),
                .visible = existing == states.end() ? true : existing->visible,
        });
    }
    return normalized;
}

const MainLayoutPanelState* mainLayoutPanelState(
        const MainLayoutModel& model,
        std::string_view panelId) {
    const auto state = std::find_if(model.panelStates.begin(), model.panelStates.end(), [panelId](const MainLayoutPanelState& candidate) {
        return candidate.panelId == panelId;
    });
    return state == model.panelStates.end() ? nullptr : &*state;
}

} // namespace

MainLayoutModel MainLayoutModel::defaults() {
    return normalizedMainLayoutModel({});
}

std::string_view mainLayoutSidebarSideName(MainLayoutSidebarSide side) {
    return side == MainLayoutSidebarSide::Left ? "Left" : "Right";
}

std::optional<MainLayoutSidebarSide> mainLayoutSidebarSideFromName(std::string_view name) {
    if (name == "Left") {
        return MainLayoutSidebarSide::Left;
    }
    if (name == "Right") {
        return MainLayoutSidebarSide::Right;
    }
    return std::nullopt;
}

MainLayoutSidebarSide mainLayoutOppositeSidebarSide(MainLayoutSidebarSide side) {
    return side == MainLayoutSidebarSide::Left ? MainLayoutSidebarSide::Right : MainLayoutSidebarSide::Left;
}

std::optional<MainLayoutSidebarSide> mainLayoutSidebarSideForDockLocation(
        const MainLayoutModel& model,
        DockLocation location) {
    if (location == DockLocation::Sidebar) {
        return model.sidebarSide;
    }
    if (location == DockLocation::AuxiliaryBar) {
        return mainLayoutOppositeSidebarSide(model.sidebarSide);
    }
    return std::nullopt;
}

std::optional<DockLocation> mainLayoutDockLocationForSidebarSide(
        const MainLayoutModel& model,
        MainLayoutSidebarSide side) {
    if (side == model.sidebarSide) {
        return DockLocation::Sidebar;
    }
    if (side == mainLayoutOppositeSidebarSide(model.sidebarSide)) {
        return DockLocation::AuxiliaryBar;
    }
    return std::nullopt;
}

bool mainLayoutLocationIsSideDock(DockLocation location) {
    return location == DockLocation::Sidebar || location == DockLocation::AuxiliaryBar;
}

const MainLayoutSideDockModel& mainLayoutSideDock(const MainLayoutModel& model, DockLocation location) {
    if (location == DockLocation::AuxiliaryBar) {
        return model.auxiliarySidebar;
    }
    assert(location == DockLocation::Sidebar);
    return model.sidebar;
}

MainLayoutSideDockModel& mainLayoutSideDock(MainLayoutModel& model, DockLocation location) {
    if (location == DockLocation::AuxiliaryBar) {
        return model.auxiliarySidebar;
    }
    assert(location == DockLocation::Sidebar);
    return model.sidebar;
}

int mainLayoutSideDockWidthDp(const MainLayoutModel& model, DockLocation location) {
    return mainLayoutSideDock(model, location).widthDp;
}

bool mainLayoutSideDockCollapsed(const MainLayoutModel& model, DockLocation location) {
    return mainLayoutSideDock(model, location).collapsed;
}

int mainLayoutBottomDockHeightDp(const MainLayoutModel& model) {
    return model.bottomDock.heightDp;
}

bool mainLayoutBottomDockCollapsed(const MainLayoutModel& model) {
    return model.bottomDock.collapsed;
}

int mainLayoutClampedSidebarWidthDp(int widthDp) {
    return std::max(widthDp, kMainLayoutSidebarMinWidthDp);
}

int mainLayoutClampedOutlinerHeightDp(int heightDp) {
    return mainLayoutClampedSidePanelStackHeightDp(heightDp);
}

int mainLayoutClampedSidePanelStackHeightDp(int heightDp) {
    return std::clamp(heightDp, kMainLayoutPanelMinHeightDp, kMainLayoutPanelMaxHeightDp);
}

int mainLayoutClampedBottomDockHeightDp(int heightDp) {
    return std::clamp(heightDp, kMainLayoutPanelMinHeightDp, kMainLayoutPanelMaxHeightDp);
}

int mainLayoutClampedBottomSplitPanelWidthDp(int widthDp) {
    return std::max(widthDp, kMainLayoutBottomSplitPanelMinWidthDp);
}

MainLayoutModel normalizedMainLayoutModel(MainLayoutModel model) {
    model.sidebar.widthDp = mainLayoutClampedSidebarWidthDp(model.sidebar.widthDp);
    model.auxiliarySidebar.widthDp = mainLayoutClampedSidebarWidthDp(model.auxiliarySidebar.widthDp);
    model.outlinerHeightDp = mainLayoutClampedOutlinerHeightDp(model.outlinerHeightDp);
    model.bottomDock.heightDp = mainLayoutClampedBottomDockHeightDp(model.bottomDock.heightDp);
    model.panelStates = normalizedMainLayoutPanelStates(std::move(model.panelStates));
    model.dockLayout = normalizedDockLayoutModel(
            std::move(model.dockLayout),
            mainLayoutDockablePanelIds(),
            DockLocation::Sidebar);
    for (DockLocationSplitPanels& splitPanels : model.dockLayout.splitPanels) {
        for (int& sizeDp : splitPanels.panelSizesDp) {
            if (sizeDp <= 0) {
                continue;
            }
            if (splitPanels.location == DockLocation::Panel) {
                sizeDp = mainLayoutClampedBottomSplitPanelWidthDp(sizeDp);
            } else if (mainLayoutLocationIsSideDock(splitPanels.location)) {
                sizeDp = mainLayoutClampedSidePanelStackHeightDp(sizeDp);
            }
        }
    }
    return model;
}

std::optional<std::string_view> mainLayoutCanonicalPanelId(std::string_view panelId) {
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (panelId == descriptor.id) {
            return descriptor.id;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> mainLayoutPanelIdFromLegacySettings(std::string_view panelId) {
    if (const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId)) {
        return canonical;
    }
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (panelId == descriptor.legacySettingsId) {
            return descriptor.id;
        }
    }
    return std::nullopt;
}

bool mainLayoutPanelIdIsKnown(std::string_view panelId) {
    return mainLayoutCanonicalPanelId(panelId).has_value();
}

bool mainLayoutPanelIdIsViewport(std::string_view panelId) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    return canonical && *canonical == kMainLayoutPanelViewport;
}

const MainLayoutPanelDescriptorList& mainLayoutPanelDescriptors() {
    return kMainLayoutPanelDescriptors;
}

std::optional<MainLayoutPanelDescriptor> mainLayoutPanelDescriptor(std::string_view panelId) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical) {
        return std::nullopt;
    }
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (*canonical == descriptor.id) {
            return descriptor;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> mainLayoutPanelIdFromVisibilitySettingsKey(std::string_view key) {
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (!descriptor.visibilitySettingsKey.empty() && key == descriptor.visibilitySettingsKey) {
            return descriptor.id;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> mainLayoutPanelIdFromDockLocationSettingsKey(std::string_view key) {
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (!descriptor.dockLocationSettingsKey.empty() && key == descriptor.dockLocationSettingsKey) {
            return descriptor.id;
        }
    }
    return std::nullopt;
}

std::vector<std::string_view> mainLayoutDockablePanelIds() {
    std::vector<std::string_view> panelIds;
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        if (descriptor.dockable) {
            panelIds.push_back(descriptor.id);
        }
    }
    return panelIds;
}

bool mainLayoutPanelVisible(const MainLayoutModel& model, std::string_view panelId) {
    const std::optional<MainLayoutPanelDescriptor> descriptor = mainLayoutPanelDescriptor(panelId);
    if (!descriptor) {
        return false;
    }
    if (descriptor->id == kMainLayoutPanelViewport) {
        return true;
    }
    const MainLayoutPanelState* state = mainLayoutPanelState(model, descriptor->id);
    return state == nullptr ? true : state->visible;
}

bool mainLayoutSetPanelVisible(MainLayoutModel& model, std::string_view panelId, bool visible) {
    const std::optional<MainLayoutPanelDescriptor> descriptor = mainLayoutPanelDescriptor(panelId);
    if (!descriptor || !descriptor->dockable) {
        return false;
    }
    model.panelStates = normalizedMainLayoutPanelStates(std::move(model.panelStates));
    for (MainLayoutPanelState& state : model.panelStates) {
        if (state.panelId != descriptor->id) {
            continue;
        }
        if (state.visible == visible) {
            return false;
        }
        state.visible = visible;
        return true;
    }
    return false;
}

bool mainLayoutPanelVisibleInLocation(
        const MainLayoutModel& model,
        std::string_view panelId,
        DockLocation expectedLocation) {
    const std::optional<DockLocation> location = dockPanelLocation(model.dockLayout, panelId);
    return location && *location == expectedLocation && mainLayoutPanelVisible(model, panelId);
}

bool mainLayoutPanelVisibleInRenderedDock(const MainLayoutModel& model, std::string_view panelId) {
    const std::optional<DockLocation> location = dockPanelLocation(model.dockLayout, panelId);
    if (!location || !mainLayoutPanelVisibleInLocation(model, panelId, *location)) {
        return false;
    }
    if (*location == DockLocation::Panel) {
        return !mainLayoutBottomDockCollapsed(model);
    }
    return mainLayoutLocationIsSideDock(*location) && !mainLayoutSideDockCollapsed(model, *location);
}

int mainLayoutVisibleDockPanelCount(const MainLayoutModel& model, DockLocation location) {
    int count = 0;
    for (std::string_view panelId : dockPanelsInLocation(model.dockLayout, location)) {
        if (mainLayoutPanelVisibleInLocation(model, panelId, location)) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string_view> mainLayoutVisiblePanelsInLocation(
        const MainLayoutModel& model,
        DockLocation location) {
    std::vector<std::string_view> result;
    for (std::string_view panelId : dockPanelsInLocation(model.dockLayout, location)) {
        if (mainLayoutPanelVisibleInLocation(model, panelId, location)) {
            result.push_back(panelId);
        }
    }
    return result;
}

std::vector<std::string_view> mainLayoutVisibleTabPanelsInLocation(
        const MainLayoutModel& model,
        DockLocation location) {
    std::vector<std::string_view> result;
    for (std::string_view panelId : dockTabPanelsInLocation(model.dockLayout, location)) {
        if (mainLayoutPanelVisibleInLocation(model, panelId, location)) {
            result.push_back(panelId);
        }
    }
    return result;
}

UITabStripModel mainLayoutVisibleTabStripInLocation(
        const MainLayoutModel& model,
        DockLocation location) {
    const std::optional<std::string_view> activePanelId = dockActivePanelId(model.dockLayout, location);
    return UITabStripModel::fromItems(
            mainLayoutVisibleTabPanelsInLocation(model, location),
            activePanelId.value_or(std::string_view{}));
}

bool mainLayoutSideDockVisibleOnSide(const MainLayoutModel& model, MainLayoutSidebarSide side) {
    const std::optional<DockLocation> location = mainLayoutDockLocationForSidebarSide(model, side);
    return location &&
            !mainLayoutSideDockCollapsed(model, *location) &&
            mainLayoutVisibleDockPanelCount(model, *location) > 0;
}

bool mainLayoutBottomDockVisibleForLocation(const MainLayoutModel& model) {
    return !mainLayoutBottomDockCollapsed(model) &&
            mainLayoutVisibleDockPanelCount(model, DockLocation::Panel) > 0;
}

std::optional<DockLocation> mainLayoutSidePanelStackLocation(const MainLayoutModel& model) {
    for (DockLocation location : {DockLocation::Sidebar, DockLocation::AuxiliaryBar}) {
        if (!mainLayoutSideDockCollapsed(model, location) &&
                mainLayoutVisiblePanelsInLocation(model, location).size() > 1) {
            return location;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> mainLayoutLeadingSidePanelStackPanelId(
        const MainLayoutModel& model,
        DockLocation location) {
    if (!mainLayoutLocationIsSideDock(location)) {
        return std::nullopt;
    }
    const std::vector<std::string_view> panels = mainLayoutVisiblePanelsInLocation(model, location);
    if (panels.size() < 2) {
        return std::nullopt;
    }
    return panels.front();
}

int mainLayoutSidePanelStackHeightDp(const MainLayoutModel& model, DockLocation location) {
    const std::optional<std::string_view> leadingPanel =
            mainLayoutLeadingSidePanelStackPanelId(model, location);
    if (!leadingPanel) {
        return 0;
    }
    return dockSplitPanelSizeDp(model.dockLayout, location, *leadingPanel)
            .value_or(model.outlinerHeightDp);
}

} // namespace quader

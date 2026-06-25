////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/organisms/MainLayoutModel.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace quader {

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
    model.dockLayout = normalizedDockLayoutModel(
            std::move(model.dockLayout),
            mainLayoutDockablePanelIds(),
            DockLocation::Sidebar);
    for (DockLocationSplitPanels& splitPanels : model.dockLayout.splitPanels) {
        if (splitPanels.location != DockLocation::Panel) {
            continue;
        }
        for (int& sizeDp : splitPanels.panelSizesDp) {
            if (sizeDp > 0) {
                sizeDp = mainLayoutClampedBottomSplitPanelWidthDp(sizeDp);
            }
        }
    }
    return model;
}

std::optional<std::string_view> mainLayoutCanonicalPanelId(std::string_view panelId) {
    if (panelId == kMainLayoutPanelViewport) {
        return kMainLayoutPanelViewport;
    }
    if (panelId == kMainLayoutPanelOutliner) {
        return kMainLayoutPanelOutliner;
    }
    if (panelId == kMainLayoutPanelInspector) {
        return kMainLayoutPanelInspector;
    }
    return std::nullopt;
}

std::optional<std::string_view> mainLayoutPanelIdFromLegacySettings(std::string_view panelId) {
    if (const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId)) {
        return canonical;
    }
    if (panelId == "Viewport") {
        return kMainLayoutPanelViewport;
    }
    if (panelId == "Outliner") {
        return kMainLayoutPanelOutliner;
    }
    if (panelId == "Inspector") {
        return kMainLayoutPanelInspector;
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

std::vector<std::string_view> mainLayoutDockablePanelIds() {
    return {
            kMainLayoutPanelOutliner,
            kMainLayoutPanelInspector,
    };
}

} // namespace quader

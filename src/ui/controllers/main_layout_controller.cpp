////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/main_layout_controller.hpp"

#include <algorithm>
#include <iterator>
#include <optional>

namespace quader {

MainLayoutController::MainLayoutController(MainLayoutModel& model)
    : model_(model) {}

bool MainLayoutController::resizeSidebar(int widthDp) {
    return resizeSidebar(DockLocation::Sidebar, widthDp);
}

bool MainLayoutController::resizeSidebar(DockLocation location, int widthDp) {
    if (!mainLayoutLocationIsSideDock(location)) {
        return false;
    }
    const int clamped = mainLayoutClampedSidebarWidthDp(widthDp);
    MainLayoutSideDockModel& sideDock = mainLayoutSideDock(model_, location);
    if (sideDock.widthDp == clamped && !sideDock.collapsed) {
        return false;
    }
    sideDock.widthDp = clamped;
    sideDock.collapsed = false;
    return true;
}

bool MainLayoutController::resizeOutlinerPanel(int heightDp) {
    const int clamped = mainLayoutClampedOutlinerHeightDp(heightDp);
    if (model_.outlinerHeightDp == clamped) {
        return false;
    }
    model_.outlinerHeightDp = clamped;
    return true;
}

bool MainLayoutController::resizeSidePanelStack(
        DockLocation location,
        std::string_view leadingPanelId,
        int heightDp) {
    if (!mainLayoutLocationIsSideDock(location)) {
        return false;
    }
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(leadingPanelId);
    const std::optional<std::string_view> leading =
            mainLayoutLeadingSidePanelStackPanelId(model_, location);
    if (!canonical ||
            *canonical == kMainLayoutPanelViewport ||
            !leading ||
            *leading != *canonical) {
        return false;
    }

    const std::vector<std::string_view> panels = mainLayoutVisiblePanelsInLocation(model_, location);
    if (panels.size() < 2) {
        return false;
    }

    const int clamped = mainLayoutClampedSidePanelStackHeightDp(heightDp);
    const std::optional<int> currentSize =
            dockSplitPanelSizeDp(model_.dockLayout, location, *canonical);
    const bool splitChanged =
            dockSetSplitPanels(model_.dockLayout, location, panels, *canonical);
    if (currentSize && *currentSize == clamped && !splitChanged) {
        return false;
    }
    const bool sizeChanged =
            dockSetSplitPanelSizeDp(model_.dockLayout, location, *canonical, clamped);
    return splitChanged || sizeChanged;
}

bool MainLayoutController::resizeBottomDock(int heightDp) {
    const int clamped = mainLayoutClampedBottomDockHeightDp(heightDp);
    if (model_.bottomDock.heightDp == clamped && !model_.bottomDock.collapsed) {
        return false;
    }
    model_.bottomDock.heightDp = clamped;
    model_.bottomDock.collapsed = false;
    return true;
}

bool MainLayoutController::resizeBottomSplitPanel(std::string_view panelId, int widthDp) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical || *canonical == kMainLayoutPanelViewport) {
        return false;
    }
    const std::optional<DockLocation> location = dockPanelLocation(model_.dockLayout, *canonical);
    if (!location || *location != DockLocation::Panel) {
        return false;
    }

    const std::vector<std::string_view> splitPanels =
            dockSplitPanelsInLocation(model_.dockLayout, DockLocation::Panel);
    const auto splitPanel = std::find(splitPanels.begin(), splitPanels.end(), *canonical);
    if (splitPanel == splitPanels.end() || std::next(splitPanel) == splitPanels.end()) {
        return false;
    }

    return dockSetSplitPanelSizeDp(
            model_.dockLayout,
            DockLocation::Panel,
            *canonical,
            mainLayoutClampedBottomSplitPanelWidthDp(widthDp));
}

bool MainLayoutController::setSidebarSide(MainLayoutSidebarSide side) {
    if (model_.sidebarSide == side) {
        return false;
    }
    model_.sidebarSide = side;
    return true;
}

bool MainLayoutController::setSidebarCollapsed(bool collapsed) {
    return setSidebarCollapsed(DockLocation::Sidebar, collapsed);
}

bool MainLayoutController::setSidebarCollapsed(DockLocation location, bool collapsed) {
    if (!mainLayoutLocationIsSideDock(location)) {
        return false;
    }
    MainLayoutSideDockModel& sideDock = mainLayoutSideDock(model_, location);
    if (sideDock.collapsed == collapsed) {
        return false;
    }
    sideDock.collapsed = collapsed;
    return true;
}

bool MainLayoutController::setBottomDockCollapsed(bool collapsed) {
    if (model_.bottomDock.collapsed == collapsed) {
        return false;
    }
    model_.bottomDock.collapsed = collapsed;
    return true;
}

bool MainLayoutController::setPanelVisible(std::string_view panelId, bool visible) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical || *canonical == kMainLayoutPanelViewport) {
        return false;
    }
    return mainLayoutSetPanelVisible(model_, *canonical, visible);
}

bool MainLayoutController::movePanelToDockLocation(std::string_view panelId, DockLocation location) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical || *canonical == kMainLayoutPanelViewport) {
        return false;
    }
    return dockMovePanelToLocation(model_.dockLayout, *canonical, location);
}

bool MainLayoutController::movePanelToDockLocation(
        std::string_view panelId,
        DockLocation location,
        std::string_view targetPanelId,
        DockPanelInsertPosition position) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    const std::optional<std::string_view> canonicalTarget = mainLayoutCanonicalPanelId(targetPanelId);
    if (!canonical ||
            !canonicalTarget ||
            *canonical == kMainLayoutPanelViewport ||
            *canonicalTarget == kMainLayoutPanelViewport) {
        return false;
    }
    return dockMovePanelToLocationAtPanel(model_.dockLayout, *canonical, location, *canonicalTarget, position);
}

bool MainLayoutController::splitPanelWithDockPanel(
        std::string_view panelId,
        DockLocation location,
        std::string_view targetPanelId,
        DockPanelInsertPosition position) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    const std::optional<std::string_view> canonicalTarget = mainLayoutCanonicalPanelId(targetPanelId);
    if (!canonical ||
            !canonicalTarget ||
            *canonical == kMainLayoutPanelViewport ||
            *canonicalTarget == kMainLayoutPanelViewport) {
        return false;
    }
    return dockSplitPanelWithPanel(model_.dockLayout, *canonical, location, *canonicalTarget, position);
}

bool MainLayoutController::setActiveDockPanel(DockLocation location, std::string_view panelId) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical || *canonical == kMainLayoutPanelViewport) {
        return false;
    }
    return dockSetActivePanel(model_.dockLayout, location, *canonical);
}

std::optional<std::string_view> MainLayoutController::activeDockPanel(DockLocation location) const {
    return dockActivePanelId(model_.dockLayout, location);
}

std::optional<DockLocation> MainLayoutController::panelDockLocation(std::string_view panelId) const {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical || *canonical == kMainLayoutPanelViewport) {
        return std::nullopt;
    }
    return dockPanelLocation(model_.dockLayout, *canonical);
}

bool MainLayoutController::isPanelVisible(std::string_view panelId) const {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical) {
        return false;
    }
    return mainLayoutPanelVisible(model_, *canonical);
}

} // namespace quader

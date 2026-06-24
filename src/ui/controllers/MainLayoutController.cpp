////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/MainLayoutController.h"

#include <optional>

namespace quader {

MainLayoutController::MainLayoutController(MainLayoutModel& model)
    : model_(model) {}

bool MainLayoutController::resizeSidebar(int widthDp) {
    return resizeSidebar(DockLocation::Sidebar, widthDp);
}

bool MainLayoutController::resizeSidebar(DockLocation location, int widthDp) {
    const int clamped = mainLayoutClampedSidebarWidthDp(widthDp);
    if (location == DockLocation::AuxiliaryBar) {
        if (model_.auxiliarySidebarWidthDp == clamped) {
            return false;
        }
        model_.auxiliarySidebarWidthDp = clamped;
        return true;
    }
    if (location != DockLocation::Sidebar || (model_.sidebarWidthDp == clamped && !model_.sidebarCollapsed)) {
        return false;
    }
    model_.sidebarWidthDp = clamped;
    model_.sidebarCollapsed = false;
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

bool MainLayoutController::setSidebarSide(MainLayoutSidebarSide side) {
    if (model_.sidebarSide == side) {
        return false;
    }
    model_.sidebarSide = side;
    return true;
}

bool MainLayoutController::setSidebarCollapsed(bool collapsed) {
    if (model_.sidebarCollapsed == collapsed) {
        return false;
    }
    model_.sidebarCollapsed = collapsed;
    return true;
}

bool MainLayoutController::setPanelVisible(std::string_view panelId, bool visible) {
    const std::optional<std::string_view> canonical = mainLayoutCanonicalPanelId(panelId);
    if (!canonical || *canonical == kMainLayoutPanelViewport) {
        return false;
    }
    if (*canonical == kMainLayoutPanelOutliner) {
        if (model_.outlinerVisible == visible) {
            return false;
        }
        model_.outlinerVisible = visible;
        return true;
    }
    if (*canonical == kMainLayoutPanelInspector) {
        if (model_.inspectorVisible == visible) {
            return false;
        }
        model_.inspectorVisible = visible;
        return true;
    }
    return false;
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
    if (*canonical == kMainLayoutPanelViewport) {
        return true;
    }
    if (*canonical == kMainLayoutPanelOutliner) {
        return model_.outlinerVisible;
    }
    if (*canonical == kMainLayoutPanelInspector) {
        return model_.inspectorVisible;
    }
    return false;
}

} // namespace quader

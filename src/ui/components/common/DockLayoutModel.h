////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/UITabStripModel.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/// Identifies the logical host that owns a docked UI panel.
enum class DockLocation {
    Sidebar,
    Panel,
    AuxiliaryBar,
};

/// Identifies how a dragged panel should be inserted relative to an existing dock panel.
enum class DockPanelInsertPosition {
    Before,
    After,
};

/// Stores the logical placement for one dockable UI panel.
struct DockPanelPlacement {
    std::string panelId;
    DockLocation location = DockLocation::Sidebar;
};

/// Stores the active panel selection for one dock location.
struct DockLocationActivePanel {
    DockLocation location = DockLocation::Sidebar;
    std::string panelId;
};

/// Stores the visible split panes for one dock location.
struct DockLocationSplitPanels {
    DockLocation location = DockLocation::Sidebar;
    std::string tabPanelId;
    std::vector<std::string> panelIds;
    std::vector<int> panelSizesDp;
};

/// Stores backend-neutral dock placement state for known UI panels.
struct DockLayoutModel {
    std::vector<DockPanelPlacement> panels;
    std::vector<DockLocationActivePanel> activePanels;
    std::vector<DockLocationSplitPanels> splitPanels;
};

[[nodiscard]] std::string_view dockLocationName(DockLocation location);
[[nodiscard]] std::optional<DockLocation> dockLocationFromName(std::string_view name);
[[nodiscard]] DockLayoutModel normalizedDockLayoutModel(
        DockLayoutModel model,
        const std::vector<std::string_view>& requiredPanelIds,
        DockLocation defaultLocation);
[[nodiscard]] std::optional<DockLocation> dockPanelLocation(
        const DockLayoutModel& model,
        std::string_view panelId);
[[nodiscard]] std::vector<std::string_view> dockPanelsInLocation(
        const DockLayoutModel& model,
        DockLocation location);
[[nodiscard]] std::vector<std::string_view> dockTabPanelsInLocation(
        const DockLayoutModel& model,
        DockLocation location);
[[nodiscard]] std::optional<std::string_view> dockActivePanelId(
        const DockLayoutModel& model,
        DockLocation location);
[[nodiscard]] UITabStripModel dockTabStripForLocation(
        const DockLayoutModel& model,
        DockLocation location);
[[nodiscard]] std::vector<std::string_view> dockSplitPanelsInLocation(
        const DockLayoutModel& model,
        DockLocation location);
[[nodiscard]] std::optional<int> dockSplitPanelSizeDp(
        const DockLayoutModel& model,
        DockLocation location,
        std::string_view panelId);
[[nodiscard]] bool dockSetActivePanel(
        DockLayoutModel& model,
        DockLocation location,
        std::string_view panelId);
[[nodiscard]] bool dockSetSplitPanels(
        DockLayoutModel& model,
        DockLocation location,
        const std::vector<std::string_view>& panelIds);
[[nodiscard]] bool dockSetSplitPanels(
        DockLayoutModel& model,
        DockLocation location,
        const std::vector<std::string_view>& panelIds,
        std::string_view tabPanelId);
[[nodiscard]] bool dockSplitPanelWithPanel(
        DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location,
        std::string_view targetPanelId,
        DockPanelInsertPosition position);
[[nodiscard]] bool dockSetSplitPanelSizeDp(
        DockLayoutModel& model,
        DockLocation location,
        std::string_view panelId,
        int sizeDp);
[[nodiscard]] bool dockMovePanelToLocation(
        DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location);
[[nodiscard]] bool dockMovePanelToLocationAtPanel(
        DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location,
        std::string_view targetPanelId,
        DockPanelInsertPosition position);
[[nodiscard]] bool dockReorderPanels(
        DockLayoutModel& model,
        const std::vector<std::string_view>& orderedPanelIds);
[[nodiscard]] bool dockPanelIdIsKnown(
        const DockLayoutModel& model,
        std::string_view panelId);

} // namespace quader

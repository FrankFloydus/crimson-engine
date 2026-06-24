////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

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

/// Stores the logical placement for one dockable UI panel.
struct DockPanelPlacement {
    std::string panelId;
    DockLocation location = DockLocation::Sidebar;
};

/// Stores backend-neutral dock placement state for known UI panels.
struct DockLayoutModel {
    std::vector<DockPanelPlacement> panels;
    std::string activePanelId;
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
[[nodiscard]] bool dockMovePanelToLocation(
        DockLayoutModel& model,
        std::string_view panelId,
        DockLocation location);
[[nodiscard]] bool dockPanelIdIsKnown(
        const DockLayoutModel& model,
        std::string_view panelId);

} // namespace quader

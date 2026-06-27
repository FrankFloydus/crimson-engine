////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/dock_layout_model.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

inline constexpr int kMainLayoutSidebarDefaultWidthDp = 240;
inline constexpr int kMainLayoutSidebarMinWidthDp = 160;
inline constexpr int kMainLayoutOutlinerDefaultHeightDp = 320;
inline constexpr int kMainLayoutBottomDockDefaultHeightDp = 240;
inline constexpr int kMainLayoutPanelMinHeightDp = 120;
inline constexpr int kMainLayoutPanelMaxHeightDp = 900;
inline constexpr int kMainLayoutBottomSplitPanelMinWidthDp = 120;
inline constexpr int kMainLayoutViewportMinSizeDp = 120;
inline constexpr std::string_view kMainLayoutPanelViewport = "viewport";
inline constexpr std::string_view kMainLayoutPanelOutliner = "outliner";
inline constexpr std::string_view kMainLayoutPanelInspector = "inspector";

/// Identifies which side of the viewport owns the fixed main-layout sidebar.
enum class MainLayoutSidebarSide {
    Left,
    Right,
};

/// Stores shared state for one side dock, regardless of whether it is primary or auxiliary.
struct MainLayoutSideDockModel {
    int widthDp = kMainLayoutSidebarDefaultWidthDp;
    bool collapsed = false;
};

/// Stores shared state for the bottom dock surface.
struct MainLayoutBottomDockModel {
    int heightDp = kMainLayoutBottomDockDefaultHeightDp;
    bool collapsed = false;
};

/// Describes one stable panel identity and the external names that map to it.
struct MainLayoutPanelDescriptor {
    std::string_view id;
    std::string_view legacySettingsId;
    std::string_view localeKey;
    std::string_view fallbackLabel;
    std::string_view visibilitySettingsKey;
    std::string_view dockLocationSettingsKey;
    bool dockable = false;
};

/// Stores mutable per-panel state keyed by stable panel id.
struct MainLayoutPanelState {
    std::string panelId;
    bool visible = true;
};

inline constexpr std::size_t kMainLayoutPanelDescriptorCount = 3;
using MainLayoutPanelDescriptorList =
        std::array<MainLayoutPanelDescriptor, kMainLayoutPanelDescriptorCount>;

/// Stores the app UI root layout state used by the RmlUi presentation.
struct MainLayoutModel {
    MainLayoutSidebarSide sidebarSide = MainLayoutSidebarSide::Right;
    MainLayoutSideDockModel sidebar;
    MainLayoutSideDockModel auxiliarySidebar;
    MainLayoutBottomDockModel bottomDock;
    int outlinerHeightDp = kMainLayoutOutlinerDefaultHeightDp;
    std::vector<MainLayoutPanelState> panelStates;
    DockLayoutModel dockLayout;

    [[nodiscard]] static MainLayoutModel defaults();
};

[[nodiscard]] std::string_view mainLayoutSidebarSideName(MainLayoutSidebarSide side);
[[nodiscard]] std::optional<MainLayoutSidebarSide> mainLayoutSidebarSideFromName(std::string_view name);
[[nodiscard]] MainLayoutSidebarSide mainLayoutOppositeSidebarSide(MainLayoutSidebarSide side);
[[nodiscard]] std::optional<MainLayoutSidebarSide> mainLayoutSidebarSideForDockLocation(
        const MainLayoutModel& model,
        DockLocation location);
[[nodiscard]] std::optional<DockLocation> mainLayoutDockLocationForSidebarSide(
        const MainLayoutModel& model,
        MainLayoutSidebarSide side);
[[nodiscard]] bool mainLayoutLocationIsSideDock(DockLocation location);
[[nodiscard]] const MainLayoutSideDockModel& mainLayoutSideDock(
        const MainLayoutModel& model,
        DockLocation location);
[[nodiscard]] MainLayoutSideDockModel& mainLayoutSideDock(MainLayoutModel& model, DockLocation location);
[[nodiscard]] int mainLayoutSideDockWidthDp(const MainLayoutModel& model, DockLocation location);
[[nodiscard]] bool mainLayoutSideDockCollapsed(const MainLayoutModel& model, DockLocation location);
[[nodiscard]] int mainLayoutBottomDockHeightDp(const MainLayoutModel& model);
[[nodiscard]] bool mainLayoutBottomDockCollapsed(const MainLayoutModel& model);
[[nodiscard]] int mainLayoutClampedSidebarWidthDp(int widthDp);
[[nodiscard]] int mainLayoutClampedOutlinerHeightDp(int heightDp);
[[nodiscard]] int mainLayoutClampedSidePanelStackHeightDp(int heightDp);
[[nodiscard]] int mainLayoutClampedBottomDockHeightDp(int heightDp);
[[nodiscard]] int mainLayoutClampedBottomSplitPanelWidthDp(int widthDp);
[[nodiscard]] MainLayoutModel normalizedMainLayoutModel(MainLayoutModel model);
[[nodiscard]] std::optional<std::string_view> mainLayoutCanonicalPanelId(std::string_view panelId);
[[nodiscard]] std::optional<std::string_view> mainLayoutPanelIdFromLegacySettings(std::string_view panelId);
[[nodiscard]] bool mainLayoutPanelIdIsKnown(std::string_view panelId);
[[nodiscard]] bool mainLayoutPanelIdIsViewport(std::string_view panelId);
[[nodiscard]] const MainLayoutPanelDescriptorList& mainLayoutPanelDescriptors();
[[nodiscard]] std::optional<MainLayoutPanelDescriptor> mainLayoutPanelDescriptor(std::string_view panelId);
[[nodiscard]] std::optional<std::string_view> mainLayoutPanelIdFromVisibilitySettingsKey(std::string_view key);
[[nodiscard]] std::optional<std::string_view> mainLayoutPanelIdFromDockLocationSettingsKey(std::string_view key);
[[nodiscard]] std::vector<std::string_view> mainLayoutDockablePanelIds();
[[nodiscard]] bool mainLayoutPanelVisible(const MainLayoutModel& model, std::string_view panelId);
[[nodiscard]] bool mainLayoutSetPanelVisible(MainLayoutModel& model, std::string_view panelId, bool visible);
[[nodiscard]] bool mainLayoutPanelVisibleInLocation(
        const MainLayoutModel& model,
        std::string_view panelId,
        DockLocation expectedLocation);
[[nodiscard]] bool mainLayoutPanelVisibleInRenderedDock(const MainLayoutModel& model, std::string_view panelId);
[[nodiscard]] int mainLayoutVisibleDockPanelCount(const MainLayoutModel& model, DockLocation location);
[[nodiscard]] std::vector<std::string_view> mainLayoutVisiblePanelsInLocation(
        const MainLayoutModel& model,
        DockLocation location);
[[nodiscard]] std::vector<std::string_view> mainLayoutVisibleTabPanelsInLocation(
        const MainLayoutModel& model,
        DockLocation location);
[[nodiscard]] UITabStripModel mainLayoutVisibleTabStripInLocation(
        const MainLayoutModel& model,
        DockLocation location);
[[nodiscard]] bool mainLayoutSideDockVisibleOnSide(const MainLayoutModel& model, MainLayoutSidebarSide side);
[[nodiscard]] bool mainLayoutBottomDockVisibleForLocation(const MainLayoutModel& model);
[[nodiscard]] std::optional<DockLocation> mainLayoutSidePanelStackLocation(const MainLayoutModel& model);
[[nodiscard]] std::optional<std::string_view> mainLayoutLeadingSidePanelStackPanelId(
        const MainLayoutModel& model,
        DockLocation location);
[[nodiscard]] int mainLayoutSidePanelStackHeightDp(const MainLayoutModel& model, DockLocation location);

} // namespace quader

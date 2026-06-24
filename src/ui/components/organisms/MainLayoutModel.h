////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/DockLayoutModel.h"

#include <optional>
#include <string_view>
#include <vector>

namespace quader {

inline constexpr int kMainLayoutSidebarDefaultWidthDp = 240;
inline constexpr int kMainLayoutSidebarMinWidthDp = 160;
inline constexpr int kMainLayoutOutlinerDefaultHeightDp = 320;
inline constexpr int kMainLayoutBottomDockDefaultHeightDp = 240;
inline constexpr int kMainLayoutPanelMinHeightDp = 120;
inline constexpr int kMainLayoutPanelMaxHeightDp = 900;
inline constexpr int kMainLayoutViewportMinWidthDp = 160;
inline constexpr std::string_view kMainLayoutPanelViewport = "viewport";
inline constexpr std::string_view kMainLayoutPanelOutliner = "outliner";
inline constexpr std::string_view kMainLayoutPanelInspector = "inspector";

/// Identifies which side of the viewport owns the fixed main-layout sidebar.
enum class MainLayoutSidebarSide {
    Left,
    Right,
};

/// Stores the app UI root layout state used by the RmlUi presentation.
struct MainLayoutModel {
    MainLayoutSidebarSide sidebarSide = MainLayoutSidebarSide::Right;
    int sidebarWidthDp = kMainLayoutSidebarDefaultWidthDp;
    int auxiliarySidebarWidthDp = kMainLayoutSidebarDefaultWidthDp;
    int outlinerHeightDp = kMainLayoutOutlinerDefaultHeightDp;
    int bottomDockHeightDp = kMainLayoutBottomDockDefaultHeightDp;
    bool sidebarCollapsed = false;
    bool outlinerVisible = true;
    bool inspectorVisible = true;
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
[[nodiscard]] int mainLayoutSideDockWidthDp(const MainLayoutModel& model, DockLocation location);
[[nodiscard]] int mainLayoutClampedSidebarWidthDp(int widthDp);
[[nodiscard]] int mainLayoutClampedOutlinerHeightDp(int heightDp);
[[nodiscard]] int mainLayoutClampedBottomDockHeightDp(int heightDp);
[[nodiscard]] MainLayoutModel normalizedMainLayoutModel(MainLayoutModel model);
[[nodiscard]] std::optional<std::string_view> mainLayoutCanonicalPanelId(std::string_view panelId);
[[nodiscard]] bool mainLayoutPanelIdIsKnown(std::string_view panelId);
[[nodiscard]] bool mainLayoutPanelIdIsViewport(std::string_view panelId);
[[nodiscard]] std::vector<std::string_view> mainLayoutDockablePanelIds();

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/organisms/main_layout_model.hpp"

#include <string_view>

namespace quader {

/**
 * Applies root app layout mutations for the current RmlUi main layout.
 */
class MainLayoutController {
public:
    explicit MainLayoutController(MainLayoutModel& model);

    [[nodiscard]] bool resizeSidebar(int widthDp);
    [[nodiscard]] bool resizeSidebar(DockLocation location, int widthDp);
    [[nodiscard]] bool resizeOutlinerPanel(int heightDp);
    [[nodiscard]] bool resizeSidePanelStack(DockLocation location, std::string_view leadingPanelId, int heightDp);
    [[nodiscard]] bool resizeBottomDock(int heightDp);
    [[nodiscard]] bool resizeBottomSplitPanel(std::string_view panelId, int widthDp);
    [[nodiscard]] bool setSidebarSide(MainLayoutSidebarSide side);
    [[nodiscard]] bool setSidebarCollapsed(bool collapsed);
    [[nodiscard]] bool setSidebarCollapsed(DockLocation location, bool collapsed);
    [[nodiscard]] bool setBottomDockCollapsed(bool collapsed);
    [[nodiscard]] bool setPanelVisible(std::string_view panelId, bool visible);
    [[nodiscard]] bool movePanelToDockLocation(std::string_view panelId, DockLocation location);
    [[nodiscard]] bool movePanelToDockLocation(
            std::string_view panelId,
            DockLocation location,
            std::string_view targetPanelId,
            DockPanelInsertPosition position);
    [[nodiscard]] bool splitPanelWithDockPanel(
            std::string_view panelId,
            DockLocation location,
            std::string_view targetPanelId,
            DockPanelInsertPosition position);
    [[nodiscard]] bool setActiveDockPanel(DockLocation location, std::string_view panelId);
    [[nodiscard]] std::optional<std::string_view> activeDockPanel(DockLocation location) const;
    [[nodiscard]] std::optional<DockLocation> panelDockLocation(std::string_view panelId) const;
    [[nodiscard]] bool isPanelVisible(std::string_view panelId) const;

private:
    MainLayoutModel& model_;
};

} // namespace quader

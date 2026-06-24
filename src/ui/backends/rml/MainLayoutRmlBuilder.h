////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/organisms/MainLayoutModel.h"

#include <optional>
#include <string>
#include <string_view>

namespace quader {

/// Stores localized labels used when projecting the app root layout.
struct MainLayoutLabels {
    std::string viewport = "Viewport";
    std::string outliner = "Outliner";
    std::string inspector = "Inspector";
};

/// Identifies which resize handle is actively being dragged in the main layout.
enum class MainLayoutActiveResizer {
    None,
    Sidebar,
    AuxiliarySidebar,
    Panels,
};

/// Stores the transient presentation for an active panel drag.
struct MainLayoutDockDragPresentation {
    std::string panelId;
    int previewLeftDp = 0;
    int previewTopDp = 0;
    int previewWidthDp = 0;
    int previewHeightDp = 0;
    bool dragging = false;
};

/// Stores the transient insertion marker for a panel drop over an existing panel.
struct MainLayoutPanelDropPresentation {
    std::string panelId;
    DockPanelInsertPosition position = DockPanelInsertPosition::After;
};

/// Stores transient presentation state for the RmlUi main layout projection.
struct MainLayoutInteractionState {
    MainLayoutActiveResizer activeResizer = MainLayoutActiveResizer::None;
    std::optional<MainLayoutSidebarSide> sidebarDropSide;
    std::optional<MainLayoutPanelDropPresentation> panelDrop;
    std::optional<MainLayoutDockDragPresentation> dockDrag;
};

/**
 * Projects the viewport/sidebar main layout into RML for the active RmlUi backend.
 */
std::string buildMainLayoutRml(
        const MainLayoutModel& model,
        const MainLayoutLabels& labels = {},
        const MainLayoutInteractionState& interaction = {});

[[nodiscard]] std::string mainLayoutPanelElementId(std::string_view panelId);
[[nodiscard]] std::string mainLayoutPanelHeaderElementId(std::string_view panelId);
[[nodiscard]] std::string mainLayoutSideDockElementId(DockLocation location);
[[nodiscard]] std::string mainLayoutSideDockResizerElementId(DockLocation location);

} // namespace quader

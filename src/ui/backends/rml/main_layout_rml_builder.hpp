////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/ui_drag_drop.hpp"
#include "ui/components/organisms/main_layout_model.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/// Stores localized labels used when projecting the app root layout.
struct MainLayoutPanelLabel {
    std::string panelId;
    std::string label;
};

/// Stores localized labels keyed by stable panel id.
struct MainLayoutLabels {
    std::vector<MainLayoutPanelLabel> panels;
};

/// Identifies which resize handle is actively being dragged in the main layout.
enum class MainLayoutActiveResizer {
    None,
    Sidebar,
    AuxiliarySidebar,
    Panels,
    BottomDock,
    BottomSplitPanel,
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
    DockLocation location = DockLocation::Sidebar;
    std::string panelId;
    DockPanelInsertPosition position = DockPanelInsertPosition::After;
    UIDropFeedbackKind feedbackKind = UIDropFeedbackKind::Target;
};

/// Stores transient presentation state for the RmlUi main layout projection.
struct MainLayoutInteractionState {
    MainLayoutActiveResizer activeResizer = MainLayoutActiveResizer::None;
    std::string activeBottomSplitPanelId;
    std::optional<DockLocation> dockDropLocation;
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
[[nodiscard]] std::string_view mainLayoutLabelForPanel(
        const MainLayoutLabels& labels,
        std::string_view panelId);
[[nodiscard]] std::string mainLayoutPanelHeaderElementId(std::string_view panelId);
[[nodiscard]] std::string mainLayoutPanelTabElementId(std::string_view panelId);
[[nodiscard]] std::string mainLayoutEditorColumnElementId();
[[nodiscard]] std::string mainLayoutModelingModeShelfSlotElementId();
[[nodiscard]] std::string mainLayoutSideDockElementId(DockLocation location);
[[nodiscard]] std::string mainLayoutSideDockResizerElementId(DockLocation location);
[[nodiscard]] std::string mainLayoutBottomDockElementId();
[[nodiscard]] std::string mainLayoutBottomDockTabStripElementId();
[[nodiscard]] std::string mainLayoutBottomDockContentElementId();
[[nodiscard]] std::string mainLayoutBottomDockResizerElementId();
[[nodiscard]] std::string mainLayoutBottomSplitResizerElementId(std::string_view panelId);

} // namespace quader

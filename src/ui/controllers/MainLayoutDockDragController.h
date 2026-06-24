////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/UIGeometry.h"
#include "ui/components/organisms/MainLayoutModel.h"
#include "ui/controllers/UIDragDropController.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/// Stores one measured panel header that can start a MainLayout drag gesture.
struct MainLayoutDockDragHitRegion {
    std::string panelId;
    UIPixelRect rect;
};

/// Stores one physical dock edge that accepts a MainLayout drag gesture.
struct MainLayoutDockDropZone {
    DockLocation location = DockLocation::Sidebar;
    UIPixelRect rect;
    UIDropFeedbackKind feedbackKind = UIDropFeedbackKind::Edge;
};

/// Identifies the layout operation a MainLayout drop target performs.
enum class MainLayoutDockDropAction {
    MoveToDock,
    InsertPanel,
    SplitPanel,
};

/// Stores one visible panel surface that accepts before/after panel insertion.
struct MainLayoutPanelDropZone {
    DockLocation location = DockLocation::Sidebar;
    std::string panelId;
    UIPixelRect rect;
    MainLayoutDockDropAction action = MainLayoutDockDropAction::InsertPanel;
    UIDropPlacementPolicy placementPolicy = UIDropPlacementPolicy::VerticalBeforeAfter;
    UIDropFeedbackKind feedbackKind = UIDropFeedbackKind::Target;
};

/// Identifies the active panel-drag drop target.
struct MainLayoutDockDropTarget {
    DockLocation location = DockLocation::Sidebar;
    std::string targetPanelId;
    DockPanelInsertPosition position = DockPanelInsertPosition::After;
    MainLayoutDockDropAction action = MainLayoutDockDropAction::MoveToDock;
    UIDropFeedbackKind feedbackKind = UIDropFeedbackKind::Target;
};

/// Carries a completed panel drop onto a sidebar edge or panel insertion target.
struct MainLayoutDockDropResult {
    std::string panelId;
    MainLayoutDockDropTarget target;
};

/**
 * Translates measured panel-header drags into backend-neutral sidebar drop targets.
 */
class MainLayoutDockDragController {
public:
    void setHitRegions(std::vector<MainLayoutDockDragHitRegion> regions);
    void setDockDropZones(std::vector<MainLayoutDockDropZone> zones);
    void setPanelDropZones(std::vector<MainLayoutPanelDropZone> zones);
    void clearHitRegions();
    void clearDockDropZones();
    void clearPanelDropZones();

    [[nodiscard]] bool beginDragCandidate(float pointerX, float pointerY);
    [[nodiscard]] std::optional<MainLayoutDockDropTarget> updateDrag(float pointerX, float pointerY);
    [[nodiscard]] std::optional<MainLayoutDockDropResult> endDrag(float pointerX, float pointerY);
    void cancel();

    [[nodiscard]] bool active() const { return dragDrop_.active(); }
    [[nodiscard]] bool dragging() const { return dragDrop_.dragging(); }
    [[nodiscard]] std::string_view activePanelId() const;
    [[nodiscard]] std::optional<MainLayoutDockDropTarget> target() const;

private:
    /// Connects a generic drop-zone identity to the dock operation it represents.
    struct DropTargetBinding {
        UIDropZoneId id;
        MainLayoutDockDropTarget target;
    };

    void rebuildDropTargets();
    void appendDropZone(
            std::vector<UIDropZoneRegion>& zones,
            MainLayoutDockDropTarget target,
            UIPixelRect rect,
            UIDropZoneKind kind,
            UIDropPlacementPolicy placementPolicy,
            UIDropFeedbackKind feedbackKind,
            int priority);
    [[nodiscard]] std::optional<MainLayoutDockDropTarget> targetForDropTarget(
            const UIDragDropTarget& target,
            std::string_view activePanelId) const;

    std::vector<MainLayoutDockDropZone> dockDropZones_;
    std::vector<MainLayoutPanelDropZone> panelDropZones_;
    std::vector<DropTargetBinding> dropTargetBindings_;
    UIDragDropController dragDrop_;
};

} // namespace quader

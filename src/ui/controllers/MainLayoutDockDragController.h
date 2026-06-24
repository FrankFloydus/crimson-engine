////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/ResizableSplit.h"
#include "ui/components/organisms/MainLayoutModel.h"

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

/// Stores one physical sidebar edge that accepts a MainLayout drag gesture.
struct MainLayoutSidebarDropZone {
    MainLayoutSidebarSide side = MainLayoutSidebarSide::Left;
    UIPixelRect rect;
};

/// Stores one visible panel surface that accepts before/after panel insertion.
struct MainLayoutPanelDropZone {
    MainLayoutSidebarSide side = MainLayoutSidebarSide::Left;
    std::string panelId;
    UIPixelRect rect;
};

/// Identifies the active panel-drag drop target.
struct MainLayoutDockDropTarget {
    MainLayoutSidebarSide side = MainLayoutSidebarSide::Left;
    std::string targetPanelId;
    DockPanelInsertPosition position = DockPanelInsertPosition::After;
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
    void setSidebarDropZones(std::vector<MainLayoutSidebarDropZone> zones);
    void setPanelDropZones(std::vector<MainLayoutPanelDropZone> zones);
    void clearHitRegions();
    void clearSidebarDropZones();
    void clearPanelDropZones();

    [[nodiscard]] bool beginDragCandidate(float pointerX, float pointerY);
    [[nodiscard]] std::optional<MainLayoutDockDropTarget> updateDrag(float pointerX, float pointerY);
    [[nodiscard]] std::optional<MainLayoutDockDropResult> endDrag(float pointerX, float pointerY);
    void cancel();

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool dragging() const { return dragging_; }
    [[nodiscard]] std::string_view activePanelId() const { return activePanelId_; }
    [[nodiscard]] std::optional<MainLayoutDockDropTarget> target() const { return target_; }

private:
    [[nodiscard]] static bool contains(const UIPixelRect& rect, float x, float y);
    [[nodiscard]] static float distanceSquared(float fromX, float fromY, float toX, float toY);
    [[nodiscard]] std::optional<MainLayoutDockDragHitRegion> hitTest(float x, float y) const;
    [[nodiscard]] std::optional<MainLayoutDockDropTarget> dropTargetAt(float x, float y) const;

    std::vector<MainLayoutDockDragHitRegion> hitRegions_;
    std::vector<MainLayoutSidebarDropZone> sidebarDropZones_;
    std::vector<MainLayoutPanelDropZone> panelDropZones_;
    std::string activePanelId_;
    float startPointerX_ = 0.0f;
    float startPointerY_ = 0.0f;
    std::optional<MainLayoutDockDropTarget> target_;
    bool active_ = false;
    bool dragging_ = false;
};

} // namespace quader

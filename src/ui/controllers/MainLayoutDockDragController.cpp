////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/MainLayoutDockDragController.h"

#include <utility>

namespace quader {
namespace {

constexpr float kDragStartThresholdPx = 4.0f;

} // namespace

void MainLayoutDockDragController::setHitRegions(std::vector<MainLayoutDockDragHitRegion> regions) {
    hitRegions_ = std::move(regions);
}

void MainLayoutDockDragController::setSidebarDropZones(std::vector<MainLayoutSidebarDropZone> zones) {
    sidebarDropZones_ = std::move(zones);
}

void MainLayoutDockDragController::setPanelDropZones(std::vector<MainLayoutPanelDropZone> zones) {
    panelDropZones_ = std::move(zones);
}

void MainLayoutDockDragController::clearHitRegions() {
    hitRegions_.clear();
}

void MainLayoutDockDragController::clearSidebarDropZones() {
    sidebarDropZones_.clear();
}

void MainLayoutDockDragController::clearPanelDropZones() {
    panelDropZones_.clear();
}

bool MainLayoutDockDragController::beginDragCandidate(float pointerX, float pointerY) {
    const std::optional<MainLayoutDockDragHitRegion> region = hitTest(pointerX, pointerY);
    if (!region) {
        return false;
    }

    activePanelId_ = region->panelId;
    startPointerX_ = pointerX;
    startPointerY_ = pointerY;
    target_ = std::nullopt;
    active_ = true;
    dragging_ = false;
    return true;
}

std::optional<MainLayoutDockDropTarget> MainLayoutDockDragController::updateDrag(float pointerX, float pointerY) {
    if (!active_) {
        return std::nullopt;
    }

    if (!dragging_ &&
            distanceSquared(startPointerX_, startPointerY_, pointerX, pointerY) <
                    kDragStartThresholdPx * kDragStartThresholdPx) {
        target_ = std::nullopt;
        return std::nullopt;
    }

    dragging_ = true;
    target_ = dropTargetAt(pointerX, pointerY);
    return target_;
}

std::optional<MainLayoutDockDropResult> MainLayoutDockDragController::endDrag(float pointerX, float pointerY) {
    if (!active_) {
        return std::nullopt;
    }

    const std::string panelId = activePanelId_;
    const std::optional<MainLayoutDockDropTarget> target = updateDrag(pointerX, pointerY);
    cancel();
    if (!target) {
        return std::nullopt;
    }

    return MainLayoutDockDropResult{
            .panelId = panelId,
            .target = *target,
    };
}

void MainLayoutDockDragController::cancel() {
    activePanelId_.clear();
    startPointerX_ = 0.0f;
    startPointerY_ = 0.0f;
    target_ = std::nullopt;
    active_ = false;
    dragging_ = false;
}

bool MainLayoutDockDragController::contains(const UIPixelRect& rect, float x, float y) {
    return x >= static_cast<float>(rect.left) &&
            y >= static_cast<float>(rect.top) &&
            x < static_cast<float>(rect.left + rect.width) &&
            y < static_cast<float>(rect.top + rect.height);
}

float MainLayoutDockDragController::distanceSquared(float fromX, float fromY, float toX, float toY) {
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    return dx * dx + dy * dy;
}

std::optional<MainLayoutDockDragHitRegion> MainLayoutDockDragController::hitTest(float x, float y) const {
    for (const MainLayoutDockDragHitRegion& region : hitRegions_) {
        if (contains(region.rect, x, y)) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<MainLayoutDockDropTarget> MainLayoutDockDragController::dropTargetAt(float x, float y) const {
    for (const MainLayoutPanelDropZone& zone : panelDropZones_) {
        if (zone.panelId == activePanelId_ || !contains(zone.rect, x, y)) {
            continue;
        }

        const float verticalMidpoint = static_cast<float>(zone.rect.top) +
                static_cast<float>(zone.rect.height) * 0.5f;
        return MainLayoutDockDropTarget{
                .side = zone.side,
                .targetPanelId = zone.panelId,
                .position = y < verticalMidpoint ? DockPanelInsertPosition::Before : DockPanelInsertPosition::After,
        };
    }

    for (const MainLayoutSidebarDropZone& zone : sidebarDropZones_) {
        if (contains(zone.rect, x, y)) {
            return MainLayoutDockDropTarget{
                    .side = zone.side,
                    .targetPanelId = {},
                    .position = DockPanelInsertPosition::After,
            };
        }
    }
    return std::nullopt;
}

} // namespace quader

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

constexpr int kDockSurfaceDropTargetPriority = 0;
constexpr int kPanelDropTargetPriority = 10;

} // namespace

void MainLayoutDockDragController::setHitRegions(std::vector<MainLayoutDockDragHitRegion> regions) {
    std::vector<UIDragSourceRegion> sources;
    sources.reserve(regions.size());
    for (const MainLayoutDockDragHitRegion& region : regions) {
        if (region.panelId.empty()) {
            continue;
        }
        sources.push_back({
                .sourceId = region.panelId,
                .item = {
                        .kind = UIDragDropItemKind::DockPanel,
                        .stableId = region.panelId,
                        .itemIds = {region.panelId},
                },
                .rect = region.rect,
        });
    }
    dragDrop_.setSources(std::move(sources));
}

void MainLayoutDockDragController::setDockDropZones(std::vector<MainLayoutDockDropZone> zones) {
    dockDropZones_ = std::move(zones);
    rebuildDropTargets();
}

void MainLayoutDockDragController::setPanelDropZones(std::vector<MainLayoutPanelDropZone> zones) {
    panelDropZones_ = std::move(zones);
    rebuildDropTargets();
}

void MainLayoutDockDragController::clearHitRegions() {
    dragDrop_.clearSources();
}

void MainLayoutDockDragController::clearDockDropZones() {
    dockDropZones_.clear();
    rebuildDropTargets();
}

void MainLayoutDockDragController::clearPanelDropZones() {
    panelDropZones_.clear();
    rebuildDropTargets();
}

bool MainLayoutDockDragController::beginDragCandidate(float pointerX, float pointerY) {
    return dragDrop_.beginCandidate({.x = pointerX, .y = pointerY});
}

std::optional<MainLayoutDockDropTarget> MainLayoutDockDragController::updateDrag(float pointerX, float pointerY) {
    const std::optional<UIDragDropTarget> currentTarget = dragDrop_.update({.x = pointerX, .y = pointerY});
    if (!currentTarget) {
        return std::nullopt;
    }

    return targetForDropTarget(*currentTarget, activePanelId());
}

std::optional<MainLayoutDockDropResult> MainLayoutDockDragController::endDrag(float pointerX, float pointerY) {
    const std::optional<UIDragDropResult> drop = dragDrop_.end({.x = pointerX, .y = pointerY});
    if (!drop) {
        return std::nullopt;
    }

    const std::optional<MainLayoutDockDropTarget> target = targetForDropTarget(drop->target, drop->item.stableId);
    if (!target) {
        return std::nullopt;
    }

    return MainLayoutDockDropResult{
            .panelId = drop->item.stableId,
            .target = *target,
    };
}

void MainLayoutDockDragController::cancel() {
    dragDrop_.cancel();
}

std::string_view MainLayoutDockDragController::activePanelId() const {
    return dragDrop_.activeItem() ? std::string_view(dragDrop_.activeItem()->stableId) : std::string_view{};
}

std::optional<MainLayoutDockDropTarget> MainLayoutDockDragController::target() const {
    const std::optional<UIDragDropTarget> currentTarget = dragDrop_.target();
    if (!currentTarget) {
        return std::nullopt;
    }
    return targetForDropTarget(*currentTarget, activePanelId());
}

void MainLayoutDockDragController::rebuildDropTargets() {
    std::vector<UIDropZoneRegion> zones;
    dropTargetBindings_.clear();
    zones.reserve(dockDropZones_.size() + panelDropZones_.size());

    for (const MainLayoutDockDropZone& zone : dockDropZones_) {
        appendDropZone(
                zones,
                MainLayoutDockDropTarget{
                        .location = zone.location,
                        .targetPanelId = {},
                        .position = DockPanelInsertPosition::After,
                        .action = MainLayoutDockDropAction::MoveToDock,
                        .feedbackKind = zone.feedbackKind,
                },
                zone.rect,
                UIDropZoneKind::DockSurface,
                UIDropPlacementPolicy::WholeRect,
                zone.feedbackKind,
                kDockSurfaceDropTargetPriority);
    }

    for (const MainLayoutPanelDropZone& zone : panelDropZones_) {
        appendDropZone(
                zones,
                MainLayoutDockDropTarget{
                        .location = zone.location,
                        .targetPanelId = zone.panelId,
                        .position = DockPanelInsertPosition::Before,
                        .action = zone.action,
                        .feedbackKind = zone.feedbackKind,
                },
                zone.rect,
                UIDropZoneKind::DockPanel,
                zone.placementPolicy,
                zone.feedbackKind,
                kPanelDropTargetPriority);
    }

    dragDrop_.setDropZones(std::move(zones));
}

void MainLayoutDockDragController::appendDropZone(
        std::vector<UIDropZoneRegion>& zones,
        MainLayoutDockDropTarget target,
        UIPixelRect rect,
        UIDropZoneKind kind,
        UIDropPlacementPolicy placementPolicy,
        UIDropFeedbackKind feedbackKind,
        int priority) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }

    const UIDropZoneId targetId{.value = static_cast<std::uint32_t>(dropTargetBindings_.size() + 1u)};
    std::optional<UIDragDropItem> representedItem;
    if (!target.targetPanelId.empty()) {
        representedItem = UIDragDropItem{
                .kind = UIDragDropItemKind::DockPanel,
                .stableId = target.targetPanelId,
                .itemIds = {target.targetPanelId},
        };
    }

    dropTargetBindings_.push_back({
            .id = targetId,
            .target = std::move(target),
    });
    zones.push_back({
            .id = targetId,
            .kind = kind,
            .rect = rect,
            .placementPolicy = placementPolicy,
            .accepts = {UIDragDropItemKind::DockPanel},
            .representedItem = std::move(representedItem),
            .rejectSameItem = true,
            .operation = UIDragDropOperation::Move,
            .feedbackKind = feedbackKind,
            .priority = priority,
    });
}

std::optional<MainLayoutDockDropTarget> MainLayoutDockDragController::targetForDropTarget(
        const UIDragDropTarget& target,
        std::string_view activePanelId) const {
    for (const DropTargetBinding& binding : dropTargetBindings_) {
        if (binding.id != target.zoneId) {
            continue;
        }
        if (!activePanelId.empty() && binding.target.targetPanelId == activePanelId) {
            return std::nullopt;
        }
        MainLayoutDockDropTarget dockTarget = binding.target;
        if (!dockTarget.targetPanelId.empty()) {
            dockTarget.position = target.placement == UIDropPlacement::Before ?
                    DockPanelInsertPosition::Before :
                    DockPanelInsertPosition::After;
        }
        return dockTarget;
    }
    return std::nullopt;
}

} // namespace quader

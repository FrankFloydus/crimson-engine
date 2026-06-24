////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/UIDragDropController.h"

#include <algorithm>
#include <utility>

namespace quader {

void UIDragDropController::setStartThresholdPx(float thresholdPx) {
    startThresholdPx_ = std::max(0.0f, thresholdPx);
}

void UIDragDropController::setSources(std::vector<UIDragSourceRegion> sources) {
    sources_ = std::move(sources);
}

void UIDragDropController::setDropZones(std::vector<UIDropZoneRegion> zones) {
    dropZones_ = std::move(zones);
    target_ = std::nullopt;
}

void UIDragDropController::clearSources() {
    sources_.clear();
}

void UIDragDropController::clearDropZones() {
    dropZones_.clear();
    target_ = std::nullopt;
}

bool UIDragDropController::beginCandidate(UIPixelPoint pointer) {
    cancel();

    const std::optional<UIDragSourceRegion> source = sourceAt(pointer.x, pointer.y);
    if (!source) {
        return false;
    }

    activeSourceId_ = source->sourceId;
    activeItem_ = source->item;
    startPointer_ = pointer;
    phase_ = UIDragDropPhase::Candidate;
    return true;
}

std::optional<UIDragDropTarget> UIDragDropController::update(UIPixelPoint pointer) {
    if (phase_ == UIDragDropPhase::Idle || !activeItem_) {
        return std::nullopt;
    }

    if (phase_ == UIDragDropPhase::Candidate &&
            distanceSquared(startPointer_.x, startPointer_.y, pointer.x, pointer.y) <
                    startThresholdPx_ * startThresholdPx_) {
        target_ = std::nullopt;
        return std::nullopt;
    }

    phase_ = UIDragDropPhase::Dragging;
    const std::optional<UIDropZoneRegion> zone = zoneAt(pointer);
    if (!zone) {
        target_ = std::nullopt;
        return std::nullopt;
    }

    target_ = targetFromZone(*zone, pointer);
    return target_;
}

std::optional<UIDragDropResult> UIDragDropController::end(UIPixelPoint pointer) {
    if (phase_ == UIDragDropPhase::Idle || !activeItem_) {
        return std::nullopt;
    }

    const UIDragDropItem item = *activeItem_;
    const std::optional<UIDragDropTarget> dropTarget = update(pointer);
    cancel();
    if (!dropTarget) {
        return std::nullopt;
    }

    return UIDragDropResult{
            .item = item,
            .target = *dropTarget,
    };
}

void UIDragDropController::cancel() {
    activeSourceId_.clear();
    activeItem_ = std::nullopt;
    startPointer_ = {};
    target_ = std::nullopt;
    phase_ = UIDragDropPhase::Idle;
}

std::optional<UIDragSourceRegion> UIDragDropController::sourceAt(float x, float y) const {
    std::optional<UIDragSourceRegion> selected;
    for (const UIDragSourceRegion& source : sources_) {
        if (!contains(source.rect, x, y)) {
            continue;
        }
        if (!selected || source.priority > selected->priority) {
            selected = source;
        }
    }
    return selected;
}

std::optional<UIDropZoneRegion> UIDragDropController::zoneAt(UIPixelPoint pointer) const {
    if (!activeItem_) {
        return std::nullopt;
    }

    std::optional<UIDropZoneRegion> selected;
    for (const UIDropZoneRegion& zone : dropZones_) {
        if (!itemAcceptedByZone(*activeItem_, zone) || !contains(zone.rect, pointer.x, pointer.y)) {
            continue;
        }
        if (!selected || zone.priority > selected->priority) {
            selected = zone;
        }
    }
    return selected;
}

UIDragDropTarget UIDragDropController::targetFromZone(
        const UIDropZoneRegion& zone,
        UIPixelPoint pointer) const {
    return UIDragDropTarget{
            .zoneId = zone.id,
            .kind = zone.kind,
            .rect = zone.rect,
            .operation = zone.operation,
            .placement = placementForZone(zone, pointer),
            .feedbackKind = zone.feedbackKind,
            .pointer = pointer,
    };
}

bool UIDragDropController::itemAcceptedByZone(
        const UIDragDropItem& item,
        const UIDropZoneRegion& zone) {
    if (zone.rejectSameItem && zone.representedItem && sameItem(item, *zone.representedItem)) {
        return false;
    }

    return zone.accepts.empty() ||
            std::find(
                    zone.accepts.begin(),
                    zone.accepts.end(),
                    item.kind) != zone.accepts.end();
}

bool UIDragDropController::sameItem(
        const UIDragDropItem& lhs,
        const UIDragDropItem& rhs) {
    if (lhs.kind != rhs.kind || rhs.stableId.empty()) {
        return false;
    }
    if (lhs.stableId == rhs.stableId) {
        return true;
    }
    return std::find(lhs.itemIds.begin(), lhs.itemIds.end(), rhs.stableId) != lhs.itemIds.end();
}

UIDropPlacement UIDragDropController::placementForZone(
        const UIDropZoneRegion& zone,
        UIPixelPoint pointer) {
    if (zone.placementPolicy == UIDropPlacementPolicy::VerticalBeforeAfter) {
        const float midpoint = static_cast<float>(zone.rect.top) + static_cast<float>(zone.rect.height) * 0.5f;
        return pointer.y < midpoint ? UIDropPlacement::Before : UIDropPlacement::After;
    }
    if (zone.placementPolicy == UIDropPlacementPolicy::HorizontalBeforeAfter) {
        const float midpoint = static_cast<float>(zone.rect.left) + static_cast<float>(zone.rect.width) * 0.5f;
        return pointer.x < midpoint ? UIDropPlacement::Before : UIDropPlacement::After;
    }
    return UIDropPlacement::On;
}

bool UIDragDropController::contains(const UIPixelRect& rect, float x, float y) {
    return rect.width > 0 &&
            rect.height > 0 &&
            x >= static_cast<float>(rect.left) &&
            y >= static_cast<float>(rect.top) &&
            x < static_cast<float>(rect.left + rect.width) &&
            y < static_cast<float>(rect.top + rect.height);
}

float UIDragDropController::distanceSquared(float fromX, float fromY, float toX, float toY) {
    const float dx = toX - fromX;
    const float dy = toY - fromY;
    return dx * dx + dy * dy;
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/ResizableSplitController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {

void ResizableSplitController::setHitRegions(std::vector<ResizableSplitHitRegion> regions) {
    hitRegions_ = std::move(regions);
}

void ResizableSplitController::clearHitRegions() {
    hitRegions_.clear();
}

std::optional<ResizableSplitHitRegion> ResizableSplitController::hitTest(float x, float y) const {
    for (const ResizableSplitHitRegion& region : hitRegions_) {
        if (contains(region.rect, x, y)) {
            return region;
        }
    }
    return std::nullopt;
}

std::optional<ResizableSplitAxis> ResizableSplitController::axisAt(float x, float y) const {
    if (const std::optional<ResizableSplitHitRegion> region = hitTest(x, y)) {
        return region->axis;
    }
    return std::nullopt;
}

void ResizableSplitController::beginDrag(
        const ResizableSplitHandle& handle,
        float pointerX,
        float pointerY,
        float dpRatio) {
    activeHandleId_ = handle.id;
    activeAxis_ = handle.axis;
    activeGrowth_ = handle.growth;
    startPointerDp_ = pointerForAxis(handle.axis, pointerX, pointerY) / std::max(0.001f, dpRatio);
    startSizeDp_ = handle.sizeDp;
    minSizeDp_ = resolveSizeRule(handle.minSize, handle.containerSizeDp);
    maxSizeDp_ = effectiveMaxSizeDp(handle, minSizeDp_);
    collapse_ = handle.collapse;
    collapseThresholdDp_ = effectiveCollapseThresholdDp(handle, minSizeDp_);
    active_ = true;
}

std::optional<ResizableSplitResizeResult> ResizableSplitController::updateDrag(
        float pointerX,
        float pointerY,
        float dpRatio) const {
    if (!active_) {
        return std::nullopt;
    }

    const float pointerDp = pointerForAxis(activeAxis_, pointerX, pointerY) / std::max(0.001f, dpRatio);
    const float deltaDp = pointerDp - startPointerDp_;
    const float signedDeltaDp = activeGrowth_ == ResizableSplitGrowthDirection::Positive ? deltaDp : -deltaDp;
    const int proposedSizeDp = static_cast<int>(std::lround(static_cast<float>(startSizeDp_) + signedDeltaDp));
    const bool collapsed = collapse_.enabled && proposedSizeDp < collapseThresholdDp_;
    const int nextSizeDp = collapsed ?
            std::max(0, collapse_.collapsedSizeDp) :
            std::clamp(proposedSizeDp, minSizeDp_, maxSizeDp_);

    return ResizableSplitResizeResult{
            .id = activeHandleId_,
            .axis = activeAxis_,
            .sizeDp = nextSizeDp,
            .collapsed = collapsed,
    };
}

void ResizableSplitController::endDrag() {
    activeHandleId_.clear();
    activeAxis_ = ResizableSplitAxis::Horizontal;
    activeGrowth_ = ResizableSplitGrowthDirection::Positive;
    startPointerDp_ = 0.0f;
    startSizeDp_ = 0;
    minSizeDp_ = 0;
    maxSizeDp_ = 0;
    collapse_ = {};
    collapseThresholdDp_ = 0;
    active_ = false;
}

std::optional<ResizableSplitAxis> ResizableSplitController::activeAxis() const {
    if (!active_) {
        return std::nullopt;
    }
    return activeAxis_;
}

UIPixelRect ResizableSplitController::inflatedHitRect(
        UIPixelRect rect,
        ResizableSplitAxis axis,
        int minThicknessPx) {
    if (axis == ResizableSplitAxis::Horizontal) {
        const int extra = std::max(0, minThicknessPx - rect.width);
        rect.left -= extra / 2;
        rect.width += extra;
        return rect;
    }

    const int extra = std::max(0, minThicknessPx - rect.height);
    rect.top -= extra / 2;
    rect.height += extra;
    return rect;
}

int ResizableSplitController::resolveSizeRule(ResizableSplitSizeRule rule, int containerSizeDp) {
    if (rule.unit == ResizableSplitSizeUnit::ContainerPercent) {
        return std::max(0,
                static_cast<int>(std::lround(
                        static_cast<float>(std::max(0, containerSizeDp)) *
                        std::max(0.0f, rule.value) /
                        100.0f)));
    }

    return std::max(0, static_cast<int>(std::lround(rule.value)));
}

int ResizableSplitController::effectiveMaxSizeDp(const ResizableSplitHandle& handle, int minSizeDp) {
    constexpr int kUnboundedMaxSizeDp = std::numeric_limits<int>::max() / 4;

    int maxSizeDp = handle.maxSize ?
            resolveSizeRule(*handle.maxSize, handle.containerSizeDp) :
            kUnboundedMaxSizeDp;
    if (handle.oppositeMinSize && handle.containerSizeDp > 0) {
        const int oppositeMinSizeDp = resolveSizeRule(*handle.oppositeMinSize, handle.containerSizeDp);
        const int maxByOppositeSide =
                handle.containerSizeDp - oppositeMinSizeDp - std::max(0, handle.handleSizeDp);
        maxSizeDp = std::min(maxSizeDp, maxByOppositeSide);
    }

    return std::max(minSizeDp, maxSizeDp);
}

int ResizableSplitController::effectiveCollapseThresholdDp(const ResizableSplitHandle& handle, int minSizeDp) {
    if (!handle.collapse.enabled) {
        return 0;
    }
    if (handle.collapse.threshold) {
        return std::max(
                std::max(0, handle.collapse.collapsedSizeDp) + 1,
                resolveSizeRule(*handle.collapse.threshold, handle.containerSizeDp));
    }

    return std::max(std::max(0, handle.collapse.collapsedSizeDp) + 1, minSizeDp / 2);
}

bool ResizableSplitController::contains(const UIPixelRect& rect, float x, float y) {
    return x >= static_cast<float>(rect.left) &&
            y >= static_cast<float>(rect.top) &&
            x < static_cast<float>(rect.left + rect.width) &&
            y < static_cast<float>(rect.top + rect.height);
}

float ResizableSplitController::pointerForAxis(ResizableSplitAxis axis, float pointerX, float pointerY) {
    return axis == ResizableSplitAxis::Horizontal ? pointerX : pointerY;
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/ResizableSplit.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Translates pointer gestures into backend-neutral resizable split size updates.
 */
class ResizableSplitController {
public:
    void setHitRegions(std::vector<ResizableSplitHitRegion> regions);
    void clearHitRegions();

    [[nodiscard]] std::optional<ResizableSplitHitRegion> hitTest(float x, float y) const;
    [[nodiscard]] std::optional<ResizableSplitAxis> axisAt(float x, float y) const;

    void beginDrag(const ResizableSplitHandle& handle, float pointerX, float pointerY, float dpRatio);
    [[nodiscard]] std::optional<ResizableSplitResizeResult> updateDrag(float pointerX, float pointerY, float dpRatio) const;
    void endDrag();

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] std::string_view activeHandleId() const { return activeHandleId_; }
    [[nodiscard]] std::optional<ResizableSplitAxis> activeAxis() const;

    [[nodiscard]] static UIPixelRect inflatedHitRect(
            UIPixelRect rect,
            ResizableSplitAxis axis,
            int minThicknessPx);

private:
    [[nodiscard]] static int resolveSizeRule(ResizableSplitSizeRule rule, int containerSizeDp);
    [[nodiscard]] static int effectiveMaxSizeDp(const ResizableSplitHandle& handle, int minSizeDp);
    [[nodiscard]] static int effectiveCollapseThresholdDp(const ResizableSplitHandle& handle, int minSizeDp);
    [[nodiscard]] static bool contains(const UIPixelRect& rect, float x, float y);
    [[nodiscard]] static float pointerForAxis(ResizableSplitAxis axis, float pointerX, float pointerY);

    std::vector<ResizableSplitHitRegion> hitRegions_;
    std::string activeHandleId_;
    ResizableSplitAxis activeAxis_ = ResizableSplitAxis::Horizontal;
    ResizableSplitGrowthDirection activeGrowth_ = ResizableSplitGrowthDirection::Positive;
    float startPointerDp_ = 0.0f;
    int startSizeDp_ = 0;
    int minSizeDp_ = 0;
    int maxSizeDp_ = 0;
    ResizableSplitCollapseOptions collapse_;
    int collapseThresholdDp_ = 0;
    bool active_ = false;
};

} // namespace quader

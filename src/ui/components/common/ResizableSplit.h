////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <optional>
#include <string>

namespace quader {

inline constexpr int kResizableSplitDefaultHitThicknessDp = 12;

/// Stores a backend-neutral rectangle in framebuffer pixels.
struct UIPixelRect {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

/// Identifies the pointer axis that changes a resizable split size.
enum class ResizableSplitAxis {
    Horizontal,
    Vertical,
};

/// Identifies whether positive pointer movement grows or shrinks the fixed split size.
enum class ResizableSplitGrowthDirection {
    Positive,
    Negative,
};

/// Identifies how a split size rule resolves against its parent container.
enum class ResizableSplitSizeUnit {
    Dp,
    ContainerPercent,
};

/// Stores one backend-neutral split size rule.
struct ResizableSplitSizeRule {
    ResizableSplitSizeUnit unit = ResizableSplitSizeUnit::Dp;
    float value = 0.0f;

    [[nodiscard]] static ResizableSplitSizeRule dp(int valueDp);
    [[nodiscard]] static ResizableSplitSizeRule percent(float value);
};

inline ResizableSplitSizeRule ResizableSplitSizeRule::dp(int valueDp) {
    return {
            .unit = ResizableSplitSizeUnit::Dp,
            .value = static_cast<float>(valueDp),
    };
}

inline ResizableSplitSizeRule ResizableSplitSizeRule::percent(float value) {
    return {
            .unit = ResizableSplitSizeUnit::ContainerPercent,
            .value = value,
    };
}

/// Stores optional snap/collapse behavior for a resizable split.
struct ResizableSplitCollapseOptions {
    bool enabled = false;
    int collapsedSizeDp = 0;
    std::optional<ResizableSplitSizeRule> threshold;
};

/// Describes one resizable split handle and its fixed-size constraints.
struct ResizableSplitHandle {
    std::string id;
    ResizableSplitAxis axis = ResizableSplitAxis::Horizontal;
    ResizableSplitGrowthDirection growth = ResizableSplitGrowthDirection::Positive;
    int sizeDp = 0;
    ResizableSplitSizeRule minSize = ResizableSplitSizeRule::dp(0);
    std::optional<ResizableSplitSizeRule> maxSize;
    std::optional<ResizableSplitSizeRule> oppositeMinSize;
    int containerSizeDp = 0;
    int handleSizeDp = 0;
    ResizableSplitCollapseOptions collapse;
};

/// Stores one measured resizable split handle hit region in framebuffer pixels.
struct ResizableSplitHitRegion {
    std::string id;
    ResizableSplitAxis axis = ResizableSplitAxis::Horizontal;
    UIPixelRect rect;
};

/// Carries the fixed-size result calculated while a split handle is dragged.
struct ResizableSplitResizeResult {
    std::string id;
    ResizableSplitAxis axis = ResizableSplitAxis::Horizontal;
    int sizeDp = 0;
    bool collapsed = false;
};

} // namespace quader

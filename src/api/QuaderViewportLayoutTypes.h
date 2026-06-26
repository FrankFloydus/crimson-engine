////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader {

/**
 * Enumerates ViewportLayoutId values used by the public viewport layout surface.
 */
enum class ViewportLayoutId {
    Single,
    TwoHorizontal,
    TwoVertical,
    ThreePrimaryRight,
    ThreePrimaryLeft,
    ThreePrimaryBottom,
    ThreePrimaryTop,
    FourQuad,
};

/**
 * Enumerates ViewportPaneId values used by the public viewport layout surface.
 */
enum class ViewportPaneId {
    Perspective,
    Top,
    Front,
    Side,
};

/**
 * Enumerates ViewportView values used by the public viewport layout surface.
 */
enum class ViewportView {
    Perspective,
    Top,
    Bottom,
    Front,
    Back,
    Side,
    Left,
};

/**
 * Represents a normalized viewport rectangle.
 */
struct ViewportNormalizedRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

/**
 * Represents a viewport layout option.
 */
struct ViewportLayoutOption {
    ViewportLayoutId id = ViewportLayoutId::Single;
    std::string_view label;
};

/**
 * Represents a viewport pane placement.
 */
struct ViewportPanePlacement {
    ViewportPaneId paneId = ViewportPaneId::Perspective;
    ViewportNormalizedRect normalizedRect;
};

/**
 * Enumerates ViewportSplitHandleId values used by the public viewport layout surface.
 */
enum class ViewportSplitHandleId {
    TwoHorizontal,
    TwoVertical,
    ThreePrimary,
    ThreeSecondary,
    FourVertical,
    FourHorizontal,
};

/**
 * Enumerates ViewportSplitHandleAxis values used by the public viewport layout surface.
 */
enum class ViewportSplitHandleAxis {
    Vertical,
    Horizontal,
};

/**
 * Represents a viewport split handle.
 */
struct ViewportSplitHandle {
    ViewportSplitHandleId id = ViewportSplitHandleId::TwoHorizontal;
    ViewportSplitHandleAxis axis = ViewportSplitHandleAxis::Vertical;
    float position = 0.5f;
    float start = 0.0f;
    float end = 1.0f;
};

/**
 * Owns viewport layout split state.
 */
class ViewportLayoutState {
public:
    static constexpr float MinSplit = 0.15f;
    static constexpr float MaxSplit = 0.85f;

    float twoHorizontalSplit() const { return twoHorizontalSplit_; }
    float twoVerticalSplit() const { return twoVerticalSplit_; }
    float threePrimarySplit() const { return threePrimarySplit_; }
    float threeSecondarySplit() const { return threeSecondarySplit_; }
    float fourVerticalSplit() const { return fourVerticalSplit_; }
    float fourHorizontalSplit() const { return fourHorizontalSplit_; }

    void setHandlePosition(ViewportSplitHandleId handleId, float normalizedPosition);

private:
    float twoHorizontalSplit_ = 0.5f;
    float twoVerticalSplit_ = 0.5f;
    float threePrimarySplit_ = 0.5f;
    float threeSecondarySplit_ = 0.5f;
    float fourVerticalSplit_ = 0.5f;
    float fourHorizontalSplit_ = 0.5f;
};

} // namespace quader

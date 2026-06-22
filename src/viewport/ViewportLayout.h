////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Enumerates ViewportLayoutId values used by the viewport layer.
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
 * Enumerates ViewportPaneId values used by the viewport layer.
 */
enum class ViewportPaneId {
    Perspective,
    Top,
    Front,
    Side,
};

/**
 * Enumerates ViewportView values used by the viewport layer.
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
 * Represents a Viewport Normalized Rect value used by the viewport input, layout, and camera layer.
 */
struct ViewportNormalizedRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

/**
 * Represents a Viewport Layout Option value used by the viewport input, layout, and camera layer.
 */
struct ViewportLayoutOption {
    ViewportLayoutId id = ViewportLayoutId::Single;
    std::string_view label;
};

/**
 * Represents a Viewport Pane Placement value used by the viewport input, layout, and camera layer.
 */
struct ViewportPanePlacement {
    ViewportPaneId paneId = ViewportPaneId::Perspective;
    ViewportNormalizedRect normalizedRect;
};

/**
 * Enumerates ViewportSplitHandleId values used by the viewport layer.
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
 * Enumerates ViewportSplitHandleAxis values used by the viewport layer.
 */
enum class ViewportSplitHandleAxis {
    Vertical,
    Horizontal,
};

/**
 * Represents a Viewport Split Handle value used by the viewport input, layout, and camera layer.
 */
struct ViewportSplitHandle {
    ViewportSplitHandleId id = ViewportSplitHandleId::TwoHorizontal;
    ViewportSplitHandleAxis axis = ViewportSplitHandleAxis::Vertical;
    float position = 0.5f;
    float start = 0.0f;
    float end = 1.0f;
};

/**
 * Owns Viewport Layout State behavior within the viewport input, layout, and camera layer.
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

constexpr std::size_t kViewportPaneCount = 4u;

std::size_t viewportPaneIndex(ViewportPaneId paneId);
const std::array<ViewportLayoutOption, 8>& viewportLayoutOptions();
std::string_view viewportLayoutStableId(ViewportLayoutId layoutId);
std::string_view viewportLayoutLabel(ViewportLayoutId layoutId);
std::string_view viewportPaneStableId(ViewportPaneId paneId);
std::string_view viewportPaneLabel(ViewportPaneId paneId);
ViewportView viewportViewForPane(ViewportPaneId paneId);
std::string_view viewportViewStableId(ViewportView view);
std::string_view viewportViewLabel(ViewportView view);
std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId);
std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId, const ViewportLayoutState& state);
std::vector<ViewportSplitHandle> viewportLayoutSplitHandles(ViewportLayoutId layoutId, const ViewportLayoutState& state);
ViewportNormalizedRect viewportAbsolutePaneRect(ViewportNormalizedRect viewportRect, ViewportNormalizedRect normalizedRect);

} // namespace quader

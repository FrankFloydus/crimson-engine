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
  ViewportPaneId pane_id = ViewportPaneId::Perspective;
  ViewportNormalizedRect normalized_rect;
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
  static constexpr float kMinSplit = 0.15f;
  static constexpr float kMaxSplit = 0.85f;

  float two_horizontal_split() const { return two_horizontal_split_; }
  float two_vertical_split() const { return two_vertical_split_; }
  float three_primary_split() const { return three_primary_split_; }
  float three_secondary_split() const { return three_secondary_split_; }
  float four_vertical_split() const { return four_vertical_split_; }
  float four_horizontal_split() const { return four_horizontal_split_; }

  void set_handle_position(ViewportSplitHandleId handle_id,
                           float normalized_position);

private:
  float two_horizontal_split_ = 0.5f;
  float two_vertical_split_ = 0.5f;
  float three_primary_split_ = 0.5f;
  float three_secondary_split_ = 0.5f;
  float four_vertical_split_ = 0.5f;
  float four_horizontal_split_ = 0.5f;
};

} // namespace quader

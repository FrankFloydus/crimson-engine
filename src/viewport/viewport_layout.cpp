////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_layout.hpp"

#include <algorithm>

namespace quader {
namespace {

constexpr std::array<ViewportLayoutOption, 8> kViewportLayoutOptions{{
        {ViewportLayoutId::Single, "Single View"},
        {ViewportLayoutId::TwoHorizontal, "Two-View Horizontal"},
        {ViewportLayoutId::TwoVertical, "Two-View Vertical"},
        {ViewportLayoutId::ThreePrimaryRight, "Three-View Primary Right"},
        {ViewportLayoutId::ThreePrimaryLeft, "Three-View Primary Left"},
        {ViewportLayoutId::ThreePrimaryBottom, "Three-View Primary Bottom"},
        {ViewportLayoutId::ThreePrimaryTop, "Three-View Primary Top"},
        {ViewportLayoutId::FourQuad, "Four-View Quad"},
}};

ViewportPanePlacement placement(ViewportPaneId pane_id, float x, float y,
                                float width, float height) {
  return {
      .pane_id = pane_id,
      .normalized_rect = {.x = x, .y = y, .width = width, .height = height},
  };
}

ViewportSplitHandle vertical_handle(ViewportSplitHandleId id, float position,
                                    float start, float end) {
  return {
      .id = id,
      .axis = ViewportSplitHandleAxis::Vertical,
      .position = position,
      .start = start,
      .end = end,
  };
}

ViewportSplitHandle horizontal_handle(ViewportSplitHandleId id, float position,
                                      float start, float end) {
  return {
      .id = id,
      .axis = ViewportSplitHandleAxis::Horizontal,
      .position = position,
      .start = start,
      .end = end,
  };
}

} // namespace

void ViewportLayoutState::set_handle_position(ViewportSplitHandleId handle_id,
                                              float normalized_position) {
  const float clamped = std::clamp(normalized_position, kMinSplit, kMaxSplit);
  switch (handle_id) {
  case ViewportSplitHandleId::TwoHorizontal:
    two_horizontal_split_ = clamped;
    break;
  case ViewportSplitHandleId::TwoVertical:
    two_vertical_split_ = clamped;
    break;
  case ViewportSplitHandleId::ThreePrimary:
    three_primary_split_ = clamped;
    break;
  case ViewportSplitHandleId::ThreeSecondary:
    three_secondary_split_ = clamped;
    break;
  case ViewportSplitHandleId::FourVertical:
    four_vertical_split_ = clamped;
    break;
  case ViewportSplitHandleId::FourHorizontal:
    four_horizontal_split_ = clamped;
    break;
  }
}

std::size_t viewport_pane_index(ViewportPaneId pane_id) {
  switch (pane_id) {
  case ViewportPaneId::Perspective:
    return 0u;
  case ViewportPaneId::Top:
    return 1u;
  case ViewportPaneId::Front:
    return 2u;
  case ViewportPaneId::Side:
    return 3u;
  }
  return 0u;
}

const std::array<ViewportLayoutOption, 8> &viewport_layout_options() {
  return kViewportLayoutOptions;
}

std::string_view viewport_layout_stable_id(ViewportLayoutId layout_id) {
  switch (layout_id) {
  case ViewportLayoutId::Single:
    return "single";
  case ViewportLayoutId::TwoHorizontal:
    return "two_horizontal";
  case ViewportLayoutId::TwoVertical:
    return "two_vertical";
  case ViewportLayoutId::ThreePrimaryRight:
    return "three_primary_right";
  case ViewportLayoutId::ThreePrimaryLeft:
    return "three_primary_left";
  case ViewportLayoutId::ThreePrimaryBottom:
    return "three_primary_bottom";
  case ViewportLayoutId::ThreePrimaryTop:
    return "three_primary_top";
  case ViewportLayoutId::FourQuad:
    return "four_quad";
  }
  return "unknown";
}

std::string_view viewport_layout_label(ViewportLayoutId layout_id) {
  const auto found =
      std::find_if(kViewportLayoutOptions.begin(), kViewportLayoutOptions.end(),
                   [layout_id](const ViewportLayoutOption &option) {
                     return option.id == layout_id;
                   });
  return found != kViewportLayoutOptions.end() ? found->label
                                               : "Unknown Layout";
}

std::string_view viewport_pane_stable_id(ViewportPaneId pane_id) {
  switch (pane_id) {
  case ViewportPaneId::Perspective:
    return "perspective";
  case ViewportPaneId::Top:
    return "top";
  case ViewportPaneId::Front:
    return "front";
  case ViewportPaneId::Side:
    return "side";
  }
  return "viewport";
}

std::string_view viewport_pane_label(ViewportPaneId pane_id) {
  switch (pane_id) {
  case ViewportPaneId::Perspective:
    return "Perspective";
  case ViewportPaneId::Top:
    return "Top";
  case ViewportPaneId::Front:
    return "Front";
  case ViewportPaneId::Side:
    return "Right";
  }
  return "Viewport";
}

ViewportView viewport_view_for_pane(ViewportPaneId pane_id) {
  switch (pane_id) {
  case ViewportPaneId::Perspective:
    return ViewportView::Perspective;
  case ViewportPaneId::Top:
    return ViewportView::Top;
  case ViewportPaneId::Front:
    return ViewportView::Front;
  case ViewportPaneId::Side:
    return ViewportView::Side;
  }
  return ViewportView::Perspective;
}

std::optional<ViewportView>
viewport_view_from_stable_id(std::string_view stable_id) {
  if (stable_id == "perspective") {
    return ViewportView::Perspective;
  }
  if (stable_id == "top") {
    return ViewportView::Top;
  }
  if (stable_id == "bottom") {
    return ViewportView::Bottom;
  }
  if (stable_id == "front") {
    return ViewportView::Front;
  }
  if (stable_id == "back") {
    return ViewportView::Back;
  }
  if (stable_id == "right") {
    return ViewportView::Side;
  }
  if (stable_id == "left") {
    return ViewportView::Left;
  }
  return std::nullopt;
}

std::vector<ViewportPanePlacement>
viewport_layout_placements(ViewportLayoutId layout_id) {
  return viewport_layout_placements(layout_id, ViewportLayoutState{});
}

std::vector<ViewportPanePlacement>
viewport_layout_placements(ViewportLayoutId layout_id,
                           const ViewportLayoutState &state) {
  switch (layout_id) {
  case ViewportLayoutId::Single:
    return {placement(ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, 1.0f)};
  case ViewportLayoutId::TwoHorizontal:
    return {
        placement(ViewportPaneId::Perspective, 0.0f, 0.0f,
                  state.two_horizontal_split(), 1.0f),
        placement(ViewportPaneId::Top, state.two_horizontal_split(), 0.0f,
                  1.0f - state.two_horizontal_split(), 1.0f),
    };
  case ViewportLayoutId::TwoVertical:
    return {
        placement(ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f,
                  state.two_vertical_split()),
        placement(ViewportPaneId::Top, 0.0f, state.two_vertical_split(), 1.0f,
                  1.0f - state.two_vertical_split()),
    };
  case ViewportLayoutId::ThreePrimaryRight:
    return {
        placement(ViewportPaneId::Top, 0.0f, 0.0f, state.three_primary_split(),
                  state.three_secondary_split()),
        placement(ViewportPaneId::Side, 0.0f, state.three_secondary_split(),
                  state.three_primary_split(),
                  1.0f - state.three_secondary_split()),
        placement(ViewportPaneId::Perspective, state.three_primary_split(),
                  0.0f, 1.0f - state.three_primary_split(), 1.0f),
    };
  case ViewportLayoutId::ThreePrimaryBottom:
    return {
        placement(ViewportPaneId::Top, 0.0f, 0.0f,
                  state.three_secondary_split(), state.three_primary_split()),
        placement(ViewportPaneId::Side, state.three_secondary_split(), 0.0f,
                  1.0f - state.three_secondary_split(),
                  state.three_primary_split()),
        placement(ViewportPaneId::Perspective, 0.0f,
                  state.three_primary_split(), 1.0f,
                  1.0f - state.three_primary_split()),
    };
  case ViewportLayoutId::ThreePrimaryTop:
    return {
        placement(ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f,
                  state.three_primary_split()),
        placement(ViewportPaneId::Top, 0.0f, state.three_primary_split(),
                  state.three_secondary_split(),
                  1.0f - state.three_primary_split()),
        placement(ViewportPaneId::Side, state.three_secondary_split(),
                  state.three_primary_split(),
                  1.0f - state.three_secondary_split(),
                  1.0f - state.three_primary_split()),
    };
  case ViewportLayoutId::FourQuad:
    return {
        placement(ViewportPaneId::Perspective, 0.0f, 0.0f,
                  state.four_vertical_split(), state.four_horizontal_split()),
        placement(ViewportPaneId::Top, state.four_vertical_split(), 0.0f,
                  1.0f - state.four_vertical_split(),
                  state.four_horizontal_split()),
        placement(ViewportPaneId::Front, 0.0f, state.four_horizontal_split(),
                  state.four_vertical_split(),
                  1.0f - state.four_horizontal_split()),
        placement(ViewportPaneId::Side, state.four_vertical_split(),
                  state.four_horizontal_split(),
                  1.0f - state.four_vertical_split(),
                  1.0f - state.four_horizontal_split()),
    };
  case ViewportLayoutId::ThreePrimaryLeft:
    return {
        placement(ViewportPaneId::Perspective, 0.0f, 0.0f,
                  state.three_primary_split(), 1.0f),
        placement(ViewportPaneId::Top, state.three_primary_split(), 0.0f,
                  1.0f - state.three_primary_split(),
                  state.three_secondary_split()),
        placement(ViewportPaneId::Side, state.three_primary_split(),
                  state.three_secondary_split(),
                  1.0f - state.three_primary_split(),
                  1.0f - state.three_secondary_split()),
    };
  }

  return {
      placement(ViewportPaneId::Perspective, 0.0f, 0.0f,
                state.three_primary_split(), 1.0f),
      placement(ViewportPaneId::Top, state.three_primary_split(), 0.0f,
                1.0f - state.three_primary_split(),
                state.three_secondary_split()),
      placement(ViewportPaneId::Side, state.three_primary_split(),
                state.three_secondary_split(),
                1.0f - state.three_primary_split(),
                1.0f - state.three_secondary_split()),
  };
}

std::vector<ViewportSplitHandle>
viewport_layout_split_handles(ViewportLayoutId layout_id,
                              const ViewportLayoutState &state) {
  switch (layout_id) {
  case ViewportLayoutId::TwoHorizontal:
    return {vertical_handle(ViewportSplitHandleId::TwoHorizontal,
                            state.two_horizontal_split(), 0.0f, 1.0f)};
  case ViewportLayoutId::TwoVertical:
    return {horizontal_handle(ViewportSplitHandleId::TwoVertical,
                              state.two_vertical_split(), 0.0f, 1.0f)};
  case ViewportLayoutId::ThreePrimaryRight:
    return {
        vertical_handle(ViewportSplitHandleId::ThreePrimary,
                        state.three_primary_split(), 0.0f, 1.0f),
        horizontal_handle(ViewportSplitHandleId::ThreeSecondary,
                          state.three_secondary_split(), 0.0f,
                          state.three_primary_split()),
    };
  case ViewportLayoutId::ThreePrimaryLeft:
    return {
        vertical_handle(ViewportSplitHandleId::ThreePrimary,
                        state.three_primary_split(), 0.0f, 1.0f),
        horizontal_handle(ViewportSplitHandleId::ThreeSecondary,
                          state.three_secondary_split(),
                          state.three_primary_split(), 1.0f),
    };
  case ViewportLayoutId::ThreePrimaryBottom:
    return {
        horizontal_handle(ViewportSplitHandleId::ThreePrimary,
                          state.three_primary_split(), 0.0f, 1.0f),
        vertical_handle(ViewportSplitHandleId::ThreeSecondary,
                        state.three_secondary_split(), 0.0f,
                        state.three_primary_split()),
    };
  case ViewportLayoutId::ThreePrimaryTop:
    return {
        horizontal_handle(ViewportSplitHandleId::ThreePrimary,
                          state.three_primary_split(), 0.0f, 1.0f),
        vertical_handle(ViewportSplitHandleId::ThreeSecondary,
                        state.three_secondary_split(),
                        state.three_primary_split(), 1.0f),
    };
  case ViewportLayoutId::FourQuad:
    return {
        vertical_handle(ViewportSplitHandleId::FourVertical,
                        state.four_vertical_split(), 0.0f, 1.0f),
        horizontal_handle(ViewportSplitHandleId::FourHorizontal,
                          state.four_horizontal_split(), 0.0f, 1.0f),
    };
  case ViewportLayoutId::Single:
    return {};
  }

  return {};
}

ViewportNormalizedRect
viewport_absolute_pane_rect(ViewportNormalizedRect viewport_rect,
                            ViewportNormalizedRect normalized_rect) {
  ViewportNormalizedRect rect{
      .x = viewport_rect.x + viewport_rect.width * normalized_rect.x,
      .y = viewport_rect.y + viewport_rect.height * normalized_rect.y,
      .width = viewport_rect.width * normalized_rect.width,
      .height = viewport_rect.height * normalized_rect.height,
  };
  const float gap_x = normalized_rect.x > 0.0f ? 1.0f : 0.0f;
  const float gap_y = normalized_rect.y > 0.0f ? 1.0f : 0.0f;
  rect.x += gap_x;
  rect.y += gap_y;
  rect.width = std::max(1.0f, rect.width - gap_x);
  rect.height = std::max(1.0f, rect.height - gap_y);
  return rect;
}

} // namespace quader

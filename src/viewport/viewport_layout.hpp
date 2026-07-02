////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace quader {

constexpr std::size_t kViewportPaneCount = 4u;

std::size_t viewport_pane_index(ViewportPaneId pane_id);
const std::array<ViewportLayoutOption, 8> &viewport_layout_options();
std::string_view viewport_layout_stable_id(ViewportLayoutId layout_id);
std::string_view viewport_layout_label(ViewportLayoutId layout_id);
std::string_view viewport_pane_stable_id(ViewportPaneId pane_id);
std::string_view viewport_pane_label(ViewportPaneId pane_id);
ViewportView viewport_view_for_pane(ViewportPaneId pane_id);
inline std::string_view viewport_view_stable_id(ViewportView view) {
  switch (view) {
  case ViewportView::Perspective:
    return "perspective";
  case ViewportView::Top:
    return "top";
  case ViewportView::Bottom:
    return "bottom";
  case ViewportView::Front:
    return "front";
  case ViewportView::Back:
    return "back";
  case ViewportView::Side:
    return "right";
  case ViewportView::Left:
    return "left";
  }
  return "perspective";
}

std::optional<ViewportView>
viewport_view_from_stable_id(std::string_view stable_id);
inline std::string_view viewport_view_label(ViewportView view) {
  switch (view) {
  case ViewportView::Perspective:
    return "Perspective";
  case ViewportView::Top:
    return "Top";
  case ViewportView::Bottom:
    return "Bottom";
  case ViewportView::Front:
    return "Front";
  case ViewportView::Back:
    return "Back";
  case ViewportView::Side:
    return "Right";
  case ViewportView::Left:
    return "Left";
  }
  return "Perspective";
}

std::vector<ViewportPanePlacement>
viewport_layout_placements(ViewportLayoutId layout_id);
std::vector<ViewportPanePlacement>
viewport_layout_placements(ViewportLayoutId layout_id,
                           const ViewportLayoutState &state);
std::vector<ViewportSplitHandle>
viewport_layout_split_handles(ViewportLayoutId layout_id,
                              const ViewportLayoutState &state);
ViewportNormalizedRect
viewport_absolute_pane_rect(ViewportNormalizedRect viewport_rect,
                            ViewportNormalizedRect normalized_rect);

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>

namespace quader {

inline constexpr int kQtViewportChromeMinimumViewWidth = 44;

/**
 * Describes the horizontal layout for one pane's floating view controls.
 * Shading has priority when the pane cannot accommodate both controls.
 */
struct QtViewportChromeLayout {
  int toolbar_x = 0;
  int toolbar_width = 0;
  int view_width = 0;
  int shading_width = 0;
  int control_gap = 0;
  bool view_visible = false;
};

inline QtViewportChromeLayout
qt_viewport_chrome_layout(int pane_width, int desired_view_width,
                          int desired_shading_width, int horizontal_inset,
                          int desired_gap) {
  const int clamped_pane_width = std::max(0, pane_width);
  const int clamped_inset = std::max(0, horizontal_inset);
  const int clamped_gap = std::max(0, desired_gap);
  const int available_width =
      clamped_pane_width > 0
          ? std::max(1, clamped_pane_width - clamped_inset * 2)
          : 0;

  QtViewportChromeLayout layout;
  if (available_width == 0) {
    return layout;
  }

  layout.shading_width =
      std::min(std::max(1, desired_shading_width), available_width);
  const int candidate_view_width = std::min(
      std::max(0, desired_view_width),
      std::max(0, available_width - layout.shading_width - clamped_gap));
  layout.view_visible =
      candidate_view_width >= kQtViewportChromeMinimumViewWidth;
  if (layout.view_visible) {
    layout.view_width = candidate_view_width;
    layout.control_gap = clamped_gap;
  }
  layout.toolbar_width =
      layout.shading_width + layout.view_width + layout.control_gap;
  layout.toolbar_x =
      std::max(0, clamped_pane_width - layout.toolbar_width - clamped_inset);
  return layout;
}

} // namespace quader

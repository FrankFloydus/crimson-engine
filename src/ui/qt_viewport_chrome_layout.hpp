////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>

namespace quader {

inline constexpr int kQtViewportDisplayMenuMinimumWidth = 148;

/**
 * Describes the horizontal layout for one pane's unified display menu.
 */
struct QtViewportChromeLayout {
  int toolbar_x = 0;
  int toolbar_width = 0;
  int menu_width = 0;
};

inline QtViewportChromeLayout
qt_viewport_chrome_layout(int pane_width, int desired_menu_width,
                          int horizontal_inset) {
  const int clamped_pane_width = std::max(0, pane_width);
  const int clamped_inset = std::max(0, horizontal_inset);
  const int available_width =
      clamped_pane_width > 0
          ? std::max(1, clamped_pane_width - clamped_inset * 2)
          : 0;

  QtViewportChromeLayout layout;
  if (available_width == 0) {
    return layout;
  }

  const int resolved_menu_width = std::max(
      kQtViewportDisplayMenuMinimumWidth, desired_menu_width);
  layout.menu_width = std::min(resolved_menu_width, available_width);
  layout.toolbar_width = layout.menu_width;
  layout.toolbar_x =
      std::max(0, clamped_pane_width - layout.toolbar_width - clamped_inset);
  return layout;
}

} // namespace quader

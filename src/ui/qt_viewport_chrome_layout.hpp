////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <cstdint>

namespace quader {

inline constexpr int kQtViewportDisplayMenuMinimumWidth = 148;

/**
 * Describes one pane's Environment and display menus, aligned to the right.
 */
struct QtViewportChromeLayout {
  int toolbar_x = 0;
  int toolbar_width = 0;
  int menu_width = 0;
  int environment_width = 0;
  int menu_spacing = 0;
};

inline QtViewportChromeLayout
qt_viewport_chrome_layout(int pane_width, int desired_menu_width,
                          int horizontal_inset,
                          int minimum_menu_width =
                              kQtViewportDisplayMenuMinimumWidth,
                          int desired_environment_width = 0,
                          int menu_spacing = 0) {
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

  const int resolved_menu_width =
      std::max(std::max(1, minimum_menu_width), desired_menu_width);
  layout.menu_width = std::min(resolved_menu_width, available_width);
  layout.toolbar_width = layout.menu_width;
  if (desired_environment_width > 0 && available_width > 1) {
    layout.menu_spacing =
        std::min(std::max(0, menu_spacing), available_width - 2);
    const std::int64_t desired_controls_width =
        static_cast<std::int64_t>(resolved_menu_width) + desired_environment_width;
    const int controls_width = static_cast<int>(std::min<std::int64_t>(
        desired_controls_width, available_width - layout.menu_spacing));
    // Share constrained panes proportionally while retaining both launchers.
    layout.environment_width = std::clamp(
        static_cast<int>(static_cast<std::int64_t>(controls_width) *
                         desired_environment_width / desired_controls_width),
        1, controls_width - 1);
    layout.menu_width = controls_width - layout.environment_width;
    layout.toolbar_width = controls_width + layout.menu_spacing;
  }
  layout.toolbar_x =
      std::max(0, clamped_pane_width - layout.toolbar_width - clamped_inset);
  return layout;
}

} // namespace quader

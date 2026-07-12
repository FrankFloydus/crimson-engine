////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <array>
#include <cstddef>

namespace quader {

/**
 * Applies native viewport activation in the order required by
 * QWidget::createWindowContainer: visibility is established for every pane
 * before any newly active child receives geometry.
 */
template <std::size_t PaneCount, typename SetVisibility, typename SyncGeometry>
void qt_apply_viewport_pane_activation(
    const std::array<bool, PaneCount> &active,
    SetVisibility &&set_visibility, SyncGeometry &&sync_geometry) {
  for (std::size_t index = 0; index < PaneCount; ++index) {
    set_visibility(index, active[index]);
  }
  for (std::size_t index = 0; index < PaneCount; ++index) {
    if (active[index]) {
      sync_geometry(index);
    }
  }
}

} // namespace quader

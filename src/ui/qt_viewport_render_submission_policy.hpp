////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"

#include <algorithm>
#include <cstddef>

namespace quader {

/**
 * Submits only the camera-navigation pane while it is active. Presenting the
 * other native swapchains is deferred until navigation ends, when the original
 * active-pane order is resumed. A stale interaction pane conservatively falls
 * back to submitting every active pane.
 */
template <typename PlacementRange, typename SubmitFrame>
std::size_t qt_submit_viewport_render_frames(
    const PlacementRange &active_placements, bool camera_navigation_active,
    ViewportPaneId interaction_pane, SubmitFrame &&submit_frame) {
  if (camera_navigation_active) {
    const auto interaction =
        std::find_if(active_placements.begin(), active_placements.end(),
                     [interaction_pane](const ViewportPanePlacement &placement) {
                       return placement.pane_id == interaction_pane;
                     });
    if (interaction != active_placements.end()) {
      return submit_frame(*interaction) ? 1u : 0u;
    }
  }

  std::size_t submitted = 0u;
  for (const ViewportPanePlacement &placement : active_placements) {
    if (submit_frame(placement)) {
      ++submitted;
    }
  }
  return submitted;
}

} // namespace quader

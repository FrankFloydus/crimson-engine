////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <algorithm>
#include <cmath>

namespace quader {

inline constexpr float kViewportOverlayDashPixels = 8.0f;
inline constexpr float kViewportOverlayDashGapPixels = 5.0f;

template <typename AppendSegment>
void append_viewport_overlay_dashed_edge(QVec2 start, QVec2 end, bool dashed,
                                         AppendSegment append_segment) {
  const QVec2 delta = end - start;
  const float edge_length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  if (edge_length <= 0.001f) {
    return;
  }
  if (!dashed) {
    append_segment(start, end, 0.0f, 1.0f);
    return;
  }

  for (float offset = 0.0f; offset < edge_length;
       offset += kViewportOverlayDashPixels + kViewportOverlayDashGapPixels) {
    const float segment_end =
        std::min(offset + kViewportOverlayDashPixels, edge_length);
    const float start_t = offset / edge_length;
    const float end_t = segment_end / edge_length;
    append_segment(start + delta * start_t, start + delta * end_t, start_t,
                   end_t);
  }
}

} // namespace quader

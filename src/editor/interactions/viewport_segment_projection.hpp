////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <optional>

namespace quader_editor_core {
struct ViewportCameraState;
}

namespace quader {

/// Projected segment endpoints in render pixels, with the viewport's top-left as origin.
struct ViewportScreenSegment {
  QVec2 start;
  QVec2 end;
  /// Parameters on the original world segment after depth and optional viewport clipping.
  double start_parameter = 0.0;
  double end_parameter = 1.0;
  /// Positive camera-space depths permit perspective-correct hit interpolation.
  double start_depth = 0.0;
  double end_depth = 0.0;
};

/**
 * Clips a world segment to the supplied camera depth range before projection.
 * When clip_to_viewport is false, offscreen endpoints remain unclamped for
 * containment tests. Parameters and depths always refer to the retained segment.
 */
[[nodiscard]] std::optional<ViewportScreenSegment> project_visible_viewport_segment(
    quader_editor_core::ViewportCameraState camera, QVec3 start, QVec3 end,
    double near_plane, double far_plane, bool clip_to_viewport = true);

} // namespace quader

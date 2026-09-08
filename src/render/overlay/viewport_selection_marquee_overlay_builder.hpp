////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

#include <span>

namespace quader {

/// Renderer-neutral marquee state projected by the selection owner.
struct RenderSelectionMarqueeRecord final {
  bool active = false;
  bool crossing = false;
  QVec2 start;
  QVec2 end;
  /// Same-call borrowing only; append() does not retain this screen-space path.
  std::span<const QVec2> lasso_points;
};

/**
 * Converts viewport selection marquee state into overlay primitives.
 */
class ViewportSelectionMarqueeOverlayBuilder {
public:
  static void append(ViewportOverlayFrame &overlay_frame,
                      const ViewportOverlayCamera &camera,
                      const RenderSelectionMarqueeRecord &marquee,
                      float raster_scale = 1.0f);
};

} // namespace quader

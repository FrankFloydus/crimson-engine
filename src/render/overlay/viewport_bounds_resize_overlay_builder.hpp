////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

namespace quader {

struct BoundsResizeFrame;
class ViewportSettings;

/// Draws only the immutable bounds targets chosen by the interaction owner.
class ViewportBoundsResizeOverlayBuilder final {
public:
  static void append(ViewportOverlayFrame &frame,
                     const ViewportOverlayCamera &camera,
                     const BoundsResizeFrame &bounds,
                     const ViewportSettings &settings);
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

namespace quader {

struct PrimitiveCreationControlsFrame;

/** Draws shared creation hit targets and owner-authored highlight polylines on top. */
class ViewportPrimitiveCreationOverlayBuilder final {
public:
  /// Screen positions and radii are already physical pixels; never scale again.
  static void append(ViewportOverlayFrame &frame,
                     const ViewportOverlayCamera &camera,
                     const PrimitiveCreationControlsFrame &controls);
};

} // namespace quader

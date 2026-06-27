////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

namespace quader {

struct ViewportSelectionMarquee;

/**
 * Converts viewport selection marquee state into overlay primitives.
 */
class ViewportSelectionMarqueeOverlayBuilder {
public:
    static void append(
            ViewportOverlayFrame& overlayFrame,
            const ViewportOverlayCamera& camera,
            const ViewportSelectionMarquee& marquee);
};

} // namespace quader

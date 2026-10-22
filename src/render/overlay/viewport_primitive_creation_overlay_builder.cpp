////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_primitive_creation_overlay_builder.hpp"

#include "render/overlay/viewport_bounds_resize_overlay_builder.hpp"
#include "viewport/primitive_creation_controls.hpp"
#include "viewport/viewport_settings.hpp"

namespace quader {

void ViewportPrimitiveCreationOverlayBuilder::append(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const PrimitiveCreationControlsFrame &controls) {
  static const ViewportSettings kSettings;
  ViewportBoundsResizeOverlayBuilder::append(frame, camera, controls.bounds, kSettings);
}

} // namespace quader

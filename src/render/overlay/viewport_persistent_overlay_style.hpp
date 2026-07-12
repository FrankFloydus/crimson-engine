////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_policy.hpp"

#include <cstdint>

namespace quader {

/**
 * Renderer-neutral style values consumed by persistent overlay caches. The
 * narrow signatures let each cache invalidate only the selected renderables
 * affected by a settings mutation.
 */
struct ViewportPersistentOverlayStyleSnapshot {
  std::uint32_t selection_face_color = kOverlaySelectionFaceColor;
  std::uint32_t selection_wire_color = kOverlaySelectionWireColor;
  float selection_edge_line_width_pixels =
      kOverlaySelectionWireLineWidthPixels;
  std::uint64_t face_signature = 0u;
  std::uint64_t wire_signature = 0u;
};

} // namespace quader

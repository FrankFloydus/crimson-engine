////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

#include <array>
#include <cstddef>

namespace quader {

/// Renderer-neutral box footprint projected by the editor/tool owner.
struct RenderBoxPreviewRecord final {
  bool valid = false;
  std::array<QVec3, 4> footprint{};
  std::size_t footprint_count = 0u;
};

/**
 * Converts the box modal tool preview into viewport overlay primitives.
 */
class ViewportBoxOverlayBuilder {
public:
  static void append(ViewportOverlayFrame &overlay_frame,
                      const ViewportOverlayCamera &camera,
                      const RenderBoxPreviewRecord &preview);
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <span>

namespace quader {

/**
 * Projects cut/mirror drawn-plane handles and plane previews into viewport overlay primitives.
 */
class DrawnPlaneOverlayProjection final {
public:
  [[nodiscard]] static QVec3
  support_point(std::span<const QVec3> visible_points, QVec3 face_normal);
  static void append(ViewportOverlayFrame &frame,
                     std::span<const QVec3> visible_points, QVec3 support_point,
                     bool has_line);
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <cmath>

namespace quader_poly {

inline constexpr double kMinSteppedFaceTextureScale = 1.0 / 8192.0;
inline constexpr double kMaxSteppedFaceTextureScale = 1024.0;

/**
 * Doubles or halves the magnitude while retaining authored reflection signs.
 * Bounds apply only in the requested direction, so manual values outside the
 * stepping interval can move inward without being changed by an outward click.
 * Invalid source values are retained for the caller's normal validation path.
 */
[[nodiscard]] inline double stepped_face_texture_scale(double scale, int steps) {
  if (steps == 0 || !std::isfinite(scale) || scale == 0.0) {
    return scale;
  }
  const double magnitude = std::abs(scale);
  if ((steps > 0 && magnitude >= kMaxSteppedFaceTextureScale) ||
      (steps < 0 && magnitude <= kMinSteppedFaceTextureScale)) {
    return scale;
  }
  // ldexp handles the complete signed step count without a per-click loop.
  const double stepped = std::ldexp(magnitude, steps);
  return std::copysign(steps > 0 ? std::min(stepped, kMaxSteppedFaceTextureScale)
                                : std::max(stepped, kMinSteppedFaceTextureScale),
                       scale);
}

} // namespace quader_poly

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/qvec3.hpp"

namespace quader {

inline bool viewport_overlay_vec3_tiny(QVec3 value, float epsilon = 0.000001f) {
  return length_squared(value) <= epsilon;
}

inline QVec3 viewport_overlay_normalize_or(QVec3 value, QVec3 fallback) {
  const float magnitude = length(value);
  return magnitude <= 0.000001f ? fallback : value / magnitude;
}

inline QVec3 viewport_overlay_fallback_perpendicular(QVec3 axis) {
  QVec3 side = cross(axis, QVec3{0.0f, 1.0f, 0.0f});
  if (viewport_overlay_vec3_tiny(side)) {
    side = cross(axis, QVec3{1.0f, 0.0f, 0.0f});
  }
  return viewport_overlay_normalize_or(side, {0.0f, 0.0f, 1.0f});
}

} // namespace quader

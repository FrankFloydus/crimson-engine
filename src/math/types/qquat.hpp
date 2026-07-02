////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/qvec3.hpp"

#include <cmath>

namespace quader {

/**
 * Represents a QQuat value used by the Quader native app.
 */
struct QQuat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr QQuat() = default;
    constexpr QQuat(float x_value, float y_value, float z_value, float w_value)
        : x(x_value), y(y_value), z(z_value), w(w_value) {}
};

inline QQuat normalize(QQuat value) {
  const float length_squared = value.x * value.x + value.y * value.y +
                               value.z * value.z + value.w * value.w;
  if (length_squared <= 0.000001f * 0.000001f) {
    return {};
  }
  const float inv_length = 1.0f / std::sqrt(length_squared);
  return {
      value.x * inv_length,
      value.y * inv_length,
      value.z * inv_length,
      value.w * inv_length,
  };
}

inline QQuat quat_from_euler_radians(QVec3 euler_radians) {
  const QVec3 c{
      std::cos(euler_radians.x * 0.5f),
      std::cos(euler_radians.y * 0.5f),
      std::cos(euler_radians.z * 0.5f),
  };
  const QVec3 s{
      std::sin(euler_radians.x * 0.5f),
      std::sin(euler_radians.y * 0.5f),
      std::sin(euler_radians.z * 0.5f),
  };
  return {
      s.x * c.y * c.z - c.x * s.y * s.z,
      c.x * s.y * c.z + s.x * c.y * s.z,
      c.x * c.y * s.z - s.x * s.y * c.z,
      c.x * c.y * c.z + s.x * s.y * s.z,
  };
}

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cmath>
#include <limits>

namespace quader {

/**
 * Validates a finite, non-degenerate camera transform without normalizing or
 * repairing it. Vector3 only needs finite x, y, and z scalar members so the
 * shared invariant remains independent of viewport, math, and persistence
 * value types.
 */
template <typename Vector3>
[[nodiscard]] inline bool camera_pose_basis_is_valid(
    const Vector3 &eye, const Vector3 &target, const Vector3 &up) noexcept {
  const auto finite = [](const Vector3 &value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };
  if (!finite(eye) || !finite(target) || !finite(up)) {
    return false;
  }

  const double forward_x =
      static_cast<double>(target.x) - static_cast<double>(eye.x);
  const double forward_y =
      static_cast<double>(target.y) - static_cast<double>(eye.y);
  const double forward_z =
      static_cast<double>(target.z) - static_cast<double>(eye.z);
  const double up_x = up.x;
  const double up_y = up.y;
  const double up_z = up.z;
  const double forward_squared = forward_x * forward_x +
                                 forward_y * forward_y +
                                 forward_z * forward_z;
  const double up_squared = up_x * up_x + up_y * up_y + up_z * up_z;
  if (!std::isfinite(forward_squared) || !std::isfinite(up_squared) ||
      forward_squared <= 1.0e-12 || up_squared <= 1.0e-12 ||
      forward_squared >
          static_cast<double>(std::numeric_limits<float>::max()) *
              static_cast<double>(std::numeric_limits<float>::max())) {
    return false;
  }

  const double cross_x = forward_y * up_z - forward_z * up_y;
  const double cross_y = forward_z * up_x - forward_x * up_z;
  const double cross_z = forward_x * up_y - forward_y * up_x;
  const double cross_squared =
      cross_x * cross_x + cross_y * cross_y + cross_z * cross_z;
  return std::isfinite(cross_squared) &&
         cross_squared > forward_squared * up_squared * 1.0e-12;
}

} // namespace quader

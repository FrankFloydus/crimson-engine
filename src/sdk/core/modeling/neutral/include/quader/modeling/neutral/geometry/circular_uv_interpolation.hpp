////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace quader_geometry {

/** Validates finite coefficients and independent planar projection covectors. */
[[nodiscard]] inline bool valid_circular_uv_frame(
    QVec3f center, QVec3f axis_u, QVec3f axis_v,
    const std::array<float, 4> &uv_from_polar) noexcept {
  const auto finite = [](QVec3f value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
  };
  if (!finite(center) || !finite(axis_u) || !finite(axis_v) ||
      !std::ranges::all_of(uv_from_polar,
                          [](float value) { return std::isfinite(value); })) return false;
  const QVec3d u{axis_u.x, axis_u.y, axis_u.z};
  const QVec3d v{axis_v.x, axis_v.y, axis_v.z};
  const auto normal = cross(u, v);
  const double scale = dot(u, u) * dot(v, v);
  return scale > 0.0 && dot(normal, normal) > scale * 1.0e-12;
}

/** Prepares x/y and their polar features for perspective-correct interpolation. */
[[nodiscard]] inline std::optional<std::array<float, 4>> circular_uv_coordinates(
    QVec3f position, QVec3f center, QVec3f axis_u, QVec3f axis_v) noexcept {
  const double dx = double(position.x) - center.x;
  const double dy = double(position.y) - center.y;
  const double dz = double(position.z) - center.z;
  const float x = static_cast<float>(dx * axis_u.x + dy * axis_u.y + dz * axis_u.z);
  const float y = static_cast<float>(dx * axis_v.x + dy * axis_v.y + dz * axis_v.z);
  const float radius = std::hypot(x, y);
  const float angle = std::atan2(y, x);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
      !std::isfinite(angle) || radius <= 0.0F) return std::nullopt;
  return std::array{x, y, radius, angle};
}

/** Matches the circular branch of the mesh shader; affine UV residuals survive. */
[[nodiscard]] inline QVec2f evaluate_circular_uv_interpolation(
    QVec2f triangle_uv, const std::array<float, 4> &coordinates,
    const std::array<float, 4> &uv_from_polar) noexcept {
  const float radius = std::hypot(coordinates[0], coordinates[1]);
  if (!std::isfinite(radius) || radius <= 0.0F) return triangle_uv;
  const float delta_radius = radius - coordinates[2];
  const float delta_angle = std::atan2(coordinates[1], coordinates[0]) - coordinates[3];
  const QVec2f corrected{
      triangle_uv.x + uv_from_polar[0] * delta_radius + uv_from_polar[1] * delta_angle,
      triangle_uv.y + uv_from_polar[2] * delta_radius + uv_from_polar[3] * delta_angle};
  return std::isfinite(corrected.x) && std::isfinite(corrected.y) ? corrected : triangle_uv;
}

} // namespace quader_geometry

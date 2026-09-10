////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/geometry/quad_uv_interpolation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace quader_geometry {
namespace {

using Double3 = std::array<double, 3>;

// These are dimensionless gates. The strongest corner supplies the basis, and
// min(J) / max(J) >= 1/32 keeps the float shader's discriminant away from zero.
constexpr double kMinimumJacobianRatio = 1.0 / 32.0;
constexpr double kMinimumBasisSineSquared = 0.000001;
constexpr double kPlanarityRelativeTolerance = 0.000001;
constexpr double kUvConvexityRelativeTolerance = 0.000001;
constexpr double kAffineUvRelativeTolerance = 0.000002;

bool finite(QVec2f value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(QVec3f value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

Double3 difference(QVec3f left, QVec3f right) noexcept {
  return {static_cast<double>(left.x) - right.x,
          static_cast<double>(left.y) - right.y,
          static_cast<double>(left.z) - right.z};
}

double dot_double(const Double3 &left, const Double3 &right) noexcept {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

Double3 cross_double(const Double3 &left, const Double3 &right) noexcept {
  return {left[1] * right[2] - left[2] * right[1],
          left[2] * right[0] - left[0] * right[2],
          left[0] * right[1] - left[1] * right[0]};
}

bool convex_uvs(const std::array<QVec2f, 4> &uvs,
                double &uv_extent_squared) noexcept {
  std::array<std::array<double, 2>, 4> edges{};
  uv_extent_squared = 0.0;
  for (std::size_t index = 0u; index < 4u; ++index) {
    const QVec2f from = uvs[index];
    const QVec2f to = uvs[(index + 1u) % 4u];
    edges[index] = {static_cast<double>(to.x) - from.x,
                    static_cast<double>(to.y) - from.y};
    uv_extent_squared =
        std::max(uv_extent_squared, edges[index][0] * edges[index][0] +
                                       edges[index][1] * edges[index][1]);
  }
  if (!std::isfinite(uv_extent_squared) || uv_extent_squared <= 0.0) {
    return false;
  }
  double orientation = 0.0;
  for (std::size_t index = 0u; index < 4u; ++index) {
    const auto &first = edges[index];
    const auto &second = edges[(index + 1u) % 4u];
    const double determinant =
        first[0] * second[1] - first[1] * second[0];
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <=
            uv_extent_squared * kUvConvexityRelativeTolerance ||
        (orientation != 0.0 && determinant * orientation <= 0.0)) {
      return false;
    }
    orientation = determinant;
  }
  return true;
}

} // namespace

std::optional<QuadUvInterpolation> build_quad_uv_interpolation(
    std::array<QVec3f, 4> points,
    std::array<QVec2f, 4> uvs) noexcept {
  if (!std::ranges::all_of(points, [](QVec3f point) { return finite(point); }) ||
      !std::ranges::all_of(uvs, [](QVec2f uv) { return finite(uv); })) {
    return std::nullopt;
  }
  double uv_extent_squared = 0.0;
  if (!convex_uvs(uvs, uv_extent_squared)) {
    return std::nullopt;
  }

  std::array<Double3, 4> corner_normals{};
  double strongest_area_squared = 0.0;
  double extent_squared = 0.0;
  std::size_t origin = 0u;
  for (std::size_t index = 0u; index < 4u; ++index) {
    const Double3 next = difference(points[(index + 1u) % 4u], points[index]);
    const Double3 previous =
        difference(points[(index + 3u) % 4u], points[index]);
    corner_normals[index] = cross_double(next, previous);
    const double area_squared =
        dot_double(corner_normals[index], corner_normals[index]);
    extent_squared = std::max(extent_squared, dot_double(next, next));
    if (area_squared > strongest_area_squared) {
      strongest_area_squared = area_squared;
      origin = index;
    }
  }
  if (!std::isfinite(strongest_area_squared) ||
      strongest_area_squared <= 0.0 || !std::isfinite(extent_squared)) {
    return std::nullopt;
  }
  const Double3 normal = corner_normals[origin];
  for (const Double3 &corner_normal : corner_normals) {
    if (dot_double(corner_normal, normal) <
        strongest_area_squared * kMinimumJacobianRatio) {
      return std::nullopt;
    }
  }
  // Choosing the largest Jacobian corner bounds h to [-1,0] on a planar
  // convex quad, reducing cancellation in the fragment quadratic solve.
  std::rotate(points.begin(), points.begin() + origin, points.end());
  std::rotate(uvs.begin(), uvs.begin() + origin, uvs.end());
  const Double3 edge_s = difference(points[1], points[0]);
  const Double3 edge_t = difference(points[3], points[0]);
  const Double3 diagonal = difference(points[2], points[0]);
  Double3 mixed{};
  for (std::size_t axis = 0u; axis < 3u; ++axis) {
    mixed[axis] = diagonal[axis] - edge_s[axis] - edge_t[axis];
  }
  const double ss = dot_double(edge_s, edge_s);
  const double st = dot_double(edge_s, edge_t);
  const double tt = dot_double(edge_t, edge_t);
  const double determinant = ss * tt - st * st;
  if (!std::isfinite(determinant) ||
      determinant <= ss * tt * kMinimumBasisSineSquared) {
    return std::nullopt;
  }
  const double sg = dot_double(edge_s, mixed);
  const double tg = dot_double(edge_t, mixed);
  const double hx = (sg * tt - tg * st) / determinant;
  const double hy = (tg * ss - sg * st) / determinant;
  const double plane_distance_numerator = dot_double(mixed, normal);
  if (!std::isfinite(hx) || !std::isfinite(hy) ||
      plane_distance_numerator * plane_distance_numerator >
          strongest_area_squared * extent_squared *
              kPlanarityRelativeTolerance * kPlanarityRelativeTolerance) {
    return std::nullopt;
  }
  const double minimum_jacobian = std::min({1.0, 1.0 + hx, 1.0 + hy,
                                            1.0 + hx + hy});
  const double maximum_jacobian = std::max({1.0, 1.0 + hx, 1.0 + hy,
                                            1.0 + hx + hy});
  if (minimum_jacobian < maximum_jacobian * kMinimumJacobianRatio) {
    return std::nullopt;
  }
  const double uv_s_x = static_cast<double>(uvs[1].x) - uvs[0].x;
  const double uv_s_y = static_cast<double>(uvs[1].y) - uvs[0].y;
  const double uv_t_x = static_cast<double>(uvs[3].x) - uvs[0].x;
  const double uv_t_y = static_cast<double>(uvs[3].y) - uvs[0].y;
  const double correction_x = static_cast<double>(uvs[2].x) - uvs[0].x -
                              uv_s_x - uv_t_x - hx * uv_s_x - hy * uv_t_x;
  const double correction_y = static_cast<double>(uvs[2].y) - uvs[0].y -
                              uv_s_y - uv_t_y - hx * uv_s_y - hy * uv_t_y;
  if (correction_x * correction_x + correction_y * correction_y <=
      uv_extent_squared * kAffineUvRelativeTolerance *
          kAffineUvRelativeTolerance) {
    return std::nullopt;
  }
  QuadUvInterpolation result;
  result.geometry_warp = {static_cast<float>(hx), static_cast<float>(hy)};
  result.uv_correction = {static_cast<float>(correction_x),
                          static_cast<float>(correction_y)};
  if (!finite(result.geometry_warp) || !finite(result.uv_correction)) {
    return std::nullopt;
  }
  constexpr std::array<QVec3f, 4> coordinates{
      QVec3f{0.0f, 0.0f, 0.0f}, QVec3f{1.0f, 0.0f, 0.0f},
      QVec3f{1.0f, 1.0f, 1.0f}, QVec3f{0.0f, 1.0f, 0.0f}};
  for (std::size_t index = 0u; index < 4u; ++index) {
    result.coordinates[(index + origin) % 4u] = coordinates[index];
  }
  return result;
}

QVec2f evaluate_quad_uv_interpolation(
    QVec2f triangle_uv, QVec3f coordinate,
    QVec2f geometry_warp, QVec2f uv_correction) noexcept {
  // Inverse bilinear interpolation, derived from P(s,t)=p0+e*s+f*t+g*s*t.
  // Concept: https://www.reedbeta.com/blog/quadrilateral-interpolation-part-2/
  // Write g=e*hx+f*hy and delta=s*t-wl. Interpolated coordinates (sl,tl,wl)
  // obey s=sl-hx*delta and t=tl-hy*delta. Solving directly for delta keeps the
  // correction exactly zero on every boundary, where sl*tl=wl. Rationalizing
  // the positive-Jacobian root also keeps the h=0 affine limit well-defined.
  const QVec2f h = geometry_warp;
  const float residual = coordinate.x * coordinate.y - coordinate.z;
  const float linear = 1.0f + h.y * coordinate.x + h.x * coordinate.y;
  const float discriminant =
      linear * linear - 4.0f * h.x * h.y * residual;
  if (!std::isfinite(discriminant) || discriminant <= 0.0f) {
    return triangle_uv;
  }
  const float denominator = linear + std::sqrt(discriminant);
  if (!std::isfinite(denominator) || denominator <= 0.0f) {
    return triangle_uv;
  }
  const float weight_delta = 2.0f * residual / denominator;
  const QVec2f result = triangle_uv + uv_correction * weight_delta;
  return finite(result) ? result : triangle_uv;
}

} // namespace quader_geometry

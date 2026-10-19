////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

namespace quader::test {

/// Stores one double-precision point in the trial parameterization.
struct SmoothUvPoint final {
  double x = 0.0;
  double y = 0.0;
};

/** One cyclic boundary corner; UVs are the effective sampled coordinates. */
struct SmoothUvBoundaryVertex final {
  SmoothUvPoint position;
  SmoothUvPoint uv;
};

/** Analytic circular reference, with U per radian and V per radial world unit. */
struct PolarUvReference final {
  SmoothUvPoint center;
  double reference_angle = 0.0;
  double reference_radius = 0.0;
  double angular_scale = 1.0;
  double radial_scale = 1.0;
  SmoothUvPoint offset;
};

/**
 * Test-only mean value interpolation over one simple planar domain, including
 * nonconvex domains. The caller owns chart extraction, seams and flattening.
 * The owned boundary has no repeated closing corner. Internal mesh edges are
 * absent from this domain; internal UV pins are not additional constraints.
 *
 * Independently implemented from the mean value formula
 * w_i = (tan(theta_{i-1}/2) + tan(theta_i/2)) / distance_i.
 * Mathematical reference: Hormann and Floater, 2006,
 * https://www.inf.usi.ch/hormann/papers/Hormann.2006.MVC.pdf .
 * No reference implementation is used. Boundary values are linear on each edge;
 * smooth interior interpolation does not promise circular isolines or injectivity.
 */
class StairsSmoothUvExperiment final {
public:
  explicit StairsSmoothUvExperiment(std::vector<SmoothUvBoundaryVertex> boundary)
      : boundary_(std::move(boundary)), valid_(prepare_boundary()) {}

  [[nodiscard]] bool valid() const noexcept { return valid_; }

  /** Outside, malformed and numerically ill-conditioned samples return no value. */
  [[nodiscard]] std::optional<SmoothUvPoint>
  evaluate_mean_value(SmoothUvPoint point) const {
    if (!valid_ || !finite(point)) return std::nullopt;
    point = {(point.x - origin_.x) / scale_, (point.y - origin_.y) / scale_};
    if (!finite(point)) return std::nullopt;
    for (const auto &corner : boundary_) {
      if (distance(point, corner.position) <= kBoundaryTolerance) return corner.uv;
    }
    bool inside = false;
    for (std::size_t index = 0U; index < boundary_.size(); ++index) {
      const auto &first = boundary_[index];
      const auto &last = boundary_[(index + 1U) % boundary_.size()];
      if (on_segment(point, first.position, last.position)) {
        const SmoothUvPoint edge = difference(last.position, first.position);
        const SmoothUvPoint relative = difference(point, first.position);
        const double fraction = (relative.x * edge.x + relative.y * edge.y) /
                                (edge.x * edge.x + edge.y * edge.y);
        const SmoothUvPoint uv = {std::lerp(first.uv.x, last.uv.x, fraction),
                                  std::lerp(first.uv.y, last.uv.y, fraction)};
        return finite(uv) ? std::optional{uv} : std::nullopt;
      }
      if ((first.position.y > point.y) != (last.position.y > point.y)) {
        const double fraction = (point.y - first.position.y) /
                                (last.position.y - first.position.y);
        if (point.x < std::lerp(first.position.x, last.position.x, fraction)) {
          inside = !inside;
        }
      }
    }
    if (!inside) return std::nullopt;

    std::vector<double> distances;
    std::vector<SmoothUvPoint> directions;
    distances.reserve(boundary_.size());
    directions.reserve(boundary_.size());
    for (const auto &corner : boundary_) {
      const SmoothUvPoint relative = difference(corner.position, point);
      const double radius = std::hypot(relative.x, relative.y);
      if (!std::isfinite(radius) || radius <= kBoundaryTolerance) return std::nullopt;
      distances.push_back(radius);
      directions.push_back({relative.x / radius, relative.y / radius});
    }
    std::vector<double> tangents;
    tangents.reserve(boundary_.size());
    for (std::size_t index = 0U; index < boundary_.size(); ++index) {
      const SmoothUvPoint first = directions[index];
      const SmoothUvPoint last = directions[(index + 1U) % boundary_.size()];
      const double sine = cross(first, last);
      const double cosine = first.x * last.x + first.y * last.y;
      if (cosine < 0.0 && sine == 0.0) return std::nullopt;
      // Equivalent half-angle identities avoid cancellation near either pole.
      const double tangent = cosine >= 0.0 ? sine / (1.0 + cosine)
                                           : (1.0 - cosine) / sine;
      if (!std::isfinite(tangent)) return std::nullopt;
      tangents.push_back(tangent);
    }
    std::vector<double> weights;
    weights.reserve(boundary_.size());
    double maximum_weight = 0.0;
    for (std::size_t index = 0U; index < boundary_.size(); ++index) {
      const std::size_t previous = (index + boundary_.size() - 1U) % boundary_.size();
      const double weight = (tangents[previous] + tangents[index]) / distances[index];
      if (!std::isfinite(weight)) return std::nullopt;
      maximum_weight = std::max(maximum_weight, std::abs(weight));
      weights.push_back(weight);
    }
    if (!(maximum_weight > 0.0)) return std::nullopt;
    double total = 0.0;
    double total_error = 0.0;
    double absolute_total = 0.0;
    SmoothUvPoint offset;
    SmoothUvPoint offset_error;
    const SmoothUvPoint anchor = boundary_.front().uv;
    for (std::size_t index = 0U; index < weights.size(); ++index) {
      const double weight = weights[index] / maximum_weight;
      const SmoothUvPoint delta = difference(boundary_[index].uv, anchor);
      if (!finite(delta)) return std::nullopt;
      add_compensated(weight, total, total_error);
      absolute_total += std::abs(weight);
      add_compensated(weight * delta.x, offset.x, offset_error.x);
      add_compensated(weight * delta.y, offset.y, offset_error.y);
    }
    // Signed weights are retained for concave domains; cancellation is an error,
    // not a reason to clamp weights or replace the map with an unrelated chart.
    if (!std::isfinite(total) || std::abs(total) <= kBoundaryTolerance * absolute_total) {
      return std::nullopt;
    }
    const SmoothUvPoint uv = {anchor.x + offset.x / total, anchor.y + offset.y / total};
    return finite(uv) ? std::optional{uv} : std::nullopt;
  }

private:
  static constexpr std::size_t kMaximumBoundaryVertices = 4096U;
  static constexpr double kBoundaryTolerance = 64.0 * std::numeric_limits<double>::epsilon();

  [[nodiscard]] static bool finite(SmoothUvPoint point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
  }

  [[nodiscard]] static SmoothUvPoint difference(SmoothUvPoint a, SmoothUvPoint b) noexcept {
    return {a.x - b.x, a.y - b.y};
  }

  [[nodiscard]] static double distance(SmoothUvPoint a, SmoothUvPoint b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
  }

  [[nodiscard]] static double cross(SmoothUvPoint a, SmoothUvPoint b) noexcept {
    return a.x * b.y - a.y * b.x;
  }

  static void add_compensated(double value, double &sum, double &error) noexcept {
    const double corrected = value - error;
    const double next = sum + corrected;
    error = (next - sum) - corrected;
    sum = next;
  }

  [[nodiscard]] static bool on_segment(SmoothUvPoint point, SmoothUvPoint first,
                                        SmoothUvPoint last) noexcept {
    const SmoothUvPoint edge = difference(last, first);
    const SmoothUvPoint relative = difference(point, first);
    const double squared_length = edge.x * edge.x + edge.y * edge.y;
    const double along = relative.x * edge.x + relative.y * edge.y;
    return along >= 0.0 && along <= squared_length &&
        std::abs(cross(edge, relative)) <= kBoundaryTolerance * std::sqrt(squared_length);
  }

  [[nodiscard]] static bool segments_intersect(SmoothUvPoint a, SmoothUvPoint b,
                                                SmoothUvPoint c, SmoothUvPoint d) noexcept {
    if (on_segment(a, c, d) || on_segment(b, c, d) ||
        on_segment(c, a, b) || on_segment(d, a, b)) return true;
    const double ab_c = cross(difference(b, a), difference(c, a));
    const double ab_d = cross(difference(b, a), difference(d, a));
    const double cd_a = cross(difference(d, c), difference(a, c));
    const double cd_b = cross(difference(d, c), difference(b, c));
    return ((ab_c > 0.0 && ab_d < 0.0) || (ab_c < 0.0 && ab_d > 0.0)) &&
           ((cd_a > 0.0 && cd_b < 0.0) || (cd_a < 0.0 && cd_b > 0.0));
  }

  [[nodiscard]] bool prepare_boundary() {
    if (boundary_.size() < 3U || boundary_.size() > kMaximumBoundaryVertices) return false;
    SmoothUvPoint minimum = boundary_.front().position;
    SmoothUvPoint maximum = minimum;
    for (const auto &corner : boundary_) {
      if (!finite(corner.position) || !finite(corner.uv)) return false;
      minimum.x = std::min(minimum.x, corner.position.x);
      minimum.y = std::min(minimum.y, corner.position.y);
      maximum.x = std::max(maximum.x, corner.position.x);
      maximum.y = std::max(maximum.y, corner.position.y);
    }
    origin_ = {std::midpoint(minimum.x, maximum.x), std::midpoint(minimum.y, maximum.y)};
    scale_ = std::max({std::abs(minimum.x - origin_.x), std::abs(maximum.x - origin_.x),
                       std::abs(minimum.y - origin_.y), std::abs(maximum.y - origin_.y)});
    if (!std::isfinite(scale_) || !(scale_ > 0.0)) return false;
    for (auto &corner : boundary_) {
      corner.position = {(corner.position.x - origin_.x) / scale_,
                          (corner.position.y - origin_.y) / scale_};
    }
    double area = 0.0;
    double area_error = 0.0;
    for (std::size_t index = 0U; index < boundary_.size(); ++index) {
      const SmoothUvPoint first = boundary_[index].position;
      const SmoothUvPoint last = boundary_[(index + 1U) % boundary_.size()].position;
      if (distance(first, last) <= kBoundaryTolerance) return false;
      add_compensated(cross(first, last), area, area_error);
      const SmoothUvPoint previous =
          boundary_[(index + boundary_.size() - 1U) % boundary_.size()].position;
      if (on_segment(last, previous, first) || on_segment(previous, first, last)) return false;
      for (std::size_t other = index + 2U; other < boundary_.size(); ++other) {
        if (index == 0U && other + 1U == boundary_.size()) continue;
        if (segments_intersect(first, last, boundary_[other].position,
                                boundary_[(other + 1U) % boundary_.size()].position)) return false;
      }
    }
    return std::isfinite(area) && std::abs(area) > kBoundaryTolerance;
  }

  std::vector<SmoothUvBoundaryVertex> boundary_;
  SmoothUvPoint origin_;
  double scale_ = 0.0;
  bool valid_ = false;
};

/** Angle is unwrapped within +/- pi of reference_angle; use one tread-sized domain. */
[[nodiscard]] inline std::optional<SmoothUvPoint>
evaluate_polar(SmoothUvPoint point, const PolarUvReference &reference) noexcept {
  for (const double value : {point.x, point.y, reference.center.x, reference.center.y,
          reference.reference_angle, reference.reference_radius, reference.angular_scale,
          reference.radial_scale, reference.offset.x, reference.offset.y}) {
    if (!std::isfinite(value)) return std::nullopt;
  }
  if (reference.angular_scale == 0.0 || reference.radial_scale == 0.0) return std::nullopt;
  const double x = point.x - reference.center.x;
  const double y = point.y - reference.center.y;
  const double radius = std::hypot(x, y);
  if (!std::isfinite(radius) || !(radius > 0.0)) return std::nullopt;
  const double angle = std::remainder(std::atan2(y, x) - reference.reference_angle,
                                      2.0 * std::numbers::pi);
  const SmoothUvPoint uv = {
      reference.offset.x + angle * reference.angular_scale,
      reference.offset.y + (radius - reference.reference_radius) * reference.radial_scale};
  return std::isfinite(uv.x) && std::isfinite(uv.y) ? std::optional{uv} : std::nullopt;
}

} // namespace quader::test

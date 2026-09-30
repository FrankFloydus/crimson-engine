////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_selection_depth.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quader {
namespace {

bool same_vector(QVec3 left, QVec3 right) noexcept {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

double precise_dot(QVec3 left, QVec3 right) noexcept {
  return static_cast<double>(left.x) * right.x +
         static_cast<double>(left.y) * right.y +
         static_cast<double>(left.z) * right.z;
}

} // namespace

bool ViewportSelectionDepthCamera::operator==(
    const ViewportSelectionDepthCamera &other) const noexcept {
  return same_vector(origin, other.origin) && same_vector(right, other.right) &&
         same_vector(up, other.up) && same_vector(forward, other.forward) &&
         viewport_size.x == other.viewport_size.x && viewport_size.y == other.viewport_size.y &&
         vertical_fov_degrees == other.vertical_fov_degrees &&
         orthographic_size == other.orthographic_size && orthographic == other.orthographic &&
         supported == other.supported;
}

bool ViewportSelectionDepthSnapshot::matches(
    const ViewportSelectionDepthGeometry *source,
    const ViewportSelectionDepthCamera &query_camera) const noexcept {
  return source != nullptr && geometry.get() == source && camera == query_camera;
}

ViewportSelectionDepthDecision classify_viewport_selection_depth(
    const ViewportSelectionDepthSnapshot &snapshot, QVec2 screen_position,
    QVec3 ray_origin, QVec3 ray_direction, double witness_distance,
    double occlusion_tolerance, ViewportSelectionDepthIntersection intersection) {
  if (!snapshot.geometry || !intersection.intersect || snapshot.width < 5u || snapshot.height < 5u ||
      !std::isfinite(witness_distance) || witness_distance < 0.0 ||
      !std::isfinite(occlusion_tolerance) || occlusion_tolerance < 0.0 ||
      !std::isfinite(snapshot.camera_depth_error) || snapshot.camera_depth_error < 0.0 ||
      !(snapshot.camera.viewport_size.x > 0.0f) || !(snapshot.camera.viewport_size.y > 0.0f)) {
    return ViewportSelectionDepthDecision::Unavailable;
  }
  const double x = static_cast<double>(screen_position.x) * snapshot.width / snapshot.camera.viewport_size.x;
  const double y = static_cast<double>(screen_position.y) * snapshot.height / snapshot.camera.viewport_size.y;
  if (!std::isfinite(x) || !std::isfinite(y) || x < 2.0 || y < 2.0 ||
      x >= snapshot.width - 2.0 || y >= snapshot.height - 2.0) {
    return ViewportSelectionDepthDecision::Unavailable;
  }
  const auto column = static_cast<std::uint32_t>(x);
  const auto row = snapshot.readback_top_left ? static_cast<std::uint32_t>(y)
      : snapshot.height - 1u - static_cast<std::uint32_t>(y);
  const std::size_t pixel = static_cast<std::size_t>(row) * snapshot.width + column;
  std::array<std::uint32_t, kViewportSelectionDepthLayers> previous_ids{};
  bool ambiguous_intersection = false;
  float previous_key = std::numeric_limits<float>::infinity();
  constexpr double kIntersectionTieTolerance = 0.000001;
  for (std::size_t layer = 0u; layer < kViewportSelectionDepthLayers; ++layer) {
    if (pixel >= snapshot.layers[layer].size()) return ViewportSelectionDepthDecision::Unavailable;
    const auto sample = snapshot.layers[layer][pixel];
    if (!std::isfinite(sample[0]) || !std::isfinite(sample[1]) ||
        sample[0] < 0.0f || sample[0] > snapshot.geometry->triangles.size() ||
        std::floor(sample[0]) != sample[0]) return ViewportSelectionDepthDecision::Unavailable;
    const auto triangle_id = static_cast<std::uint32_t>(sample[0]);
    if (triangle_id == 0u) {
      if (sample[1] != 0.0f || sample[2] != 0.0f || sample[3] != 0.0f) return ViewportSelectionDepthDecision::Unavailable;
      return snapshot.conservative_coverage && !ambiguous_intersection
          ? ViewportSelectionDepthDecision::Accepted : ViewportSelectionDepthDecision::Overflow;
    }
    if (sample[2] != 1.0f || sample[3] != 1.0f || !(sample[1] > 0.0f) || sample[1] > 1.0f || sample[1] > previous_key ||
        std::find(previous_ids.begin(), previous_ids.begin() + layer, triangle_id) != previous_ids.begin() + layer) {
      return ViewportSelectionDepthDecision::Unavailable;
    }
    previous_ids[layer] = triangle_id;
    previous_key = sample[1];
    const auto hit = intersection.intersect(intersection.owner, triangle_id, ray_origin, ray_direction);
    if (hit && std::isfinite(*hit) && *hit >= 0.0f && *hit < witness_distance - occlusion_tolerance) {
      if (*hit < witness_distance - occlusion_tolerance - kIntersectionTieTolerance) {
        return ViewportSelectionDepthDecision::Rejected;
      }
      ambiguous_intersection = true;
    }
  }
  if (!snapshot.conservative_coverage || ambiguous_intersection) return ViewportSelectionDepthDecision::Overflow;
  const double slope = precise_dot(ray_direction, snapshot.camera.forward);
  if (!(slope > 0.0) || !std::isfinite(slope)) return ViewportSelectionDepthDecision::Unavailable;
  // Use the outward edge of the DEPTH32F sorting bucket, not the unquantized
  // plane depth: another triangle can share this key and remain unpeeled.
  float outward_key = std::nextafter(previous_key, std::numeric_limits<float>::infinity());
  outward_key = std::nextafter(outward_key, std::numeric_limits<float>::infinity());
  const double lower_camera_depth = std::max(0.0, 1.0 / static_cast<double>(outward_key) - 1.0 - snapshot.camera_depth_error);
  const double origin_depth = precise_dot(ray_origin - snapshot.camera.origin, snapshot.camera.forward);
  const double lower_ray_distance = (lower_camera_depth - origin_depth) / slope;
  return lower_ray_distance >= witness_distance - occlusion_tolerance
      ? ViewportSelectionDepthDecision::Accepted : ViewportSelectionDepthDecision::Overflow;
}

} // namespace quader

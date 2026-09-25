////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_camera_projector.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace quader {

inline constexpr double kViewportSelectionDimensionMinimumMapLength = 0.01;

/** One world-axis span shared by the colored overlay and its numeric label. */
struct ViewportSelectionDimensionSpan final {
  QVec3 start;
  QVec3 end;
};

/** X/Y/Z spans; omitted depth or sub-threshold axes have no span. */
using ViewportSelectionDimensionSpans =
    std::array<std::optional<ViewportSelectionDimensionSpan>, 3>;

[[nodiscard]] inline bool viewport_selection_dimension_bounds_valid(
    const ViewportSelectionDimensionBounds &bounds) {
  return std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) &&
         std::isfinite(bounds.min.z) && std::isfinite(bounds.max.x) &&
         std::isfinite(bounds.max.y) && std::isfinite(bounds.max.z) &&
         bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y &&
         bounds.min.z <= bounds.max.z;
}

/** Selects the same incident bounds edges for both line and text passes. */
[[nodiscard]] inline ViewportSelectionDimensionSpans
viewport_selection_dimension_spans(
    const ViewportSelectionDimensionBounds &bounds,
    const ViewportOverlayCamera &camera, double map_units_per_world_unit) {
  ViewportSelectionDimensionSpans spans;
  if (!viewport_selection_dimension_bounds_valid(bounds) ||
      !std::isfinite(map_units_per_world_unit) || map_units_per_world_unit <= 0.0) {
    return spans;
  }
  const auto corner_point = [&](unsigned int corner) -> QVec3 {
    return {(corner & 1U) != 0U ? bounds.max.x : bounds.min.x,
            (corner & 2U) != 0U ? bounds.max.y : bounds.min.y,
            (corner & 4U) != 0U ? bounds.max.z : bounds.min.z};
  };
  const OverlayCameraProjector projector(camera);
  const auto &projection_camera = projector.camera();
  unsigned int chosen_corner = 0U;
  std::size_t omitted_axis = 3U;
  if (projection_camera.projection == ViewportOverlayProjection::Orthographic) {
    const QVec3 forward = projection_camera.forward;
    const std::array magnitudes{std::abs(forward.x), std::abs(forward.y),
                                std::abs(forward.z)};
    omitted_axis = static_cast<std::size_t>(
        std::max_element(magnitudes.begin(), magnitudes.end()) -
        magnitudes.begin());
  } else {
    double nearest_distance_squared = std::numeric_limits<double>::infinity();
    for (unsigned int corner = 0U; corner < 8U; ++corner) {
      const QVec3 point = corner_point(corner);
      const OverlayViewPoint view = projector.view_point_for(point);
      const float depth = -view.z;
      if (depth <= projection_camera.near_plane ||
          depth >= projection_camera.far_plane) {
        continue;
      }
      const OverlayProjectedPoint projected = projector.project(view);
      if (!projected.valid || !std::isfinite(projected.device.x) ||
          !std::isfinite(projected.device.y) || projected.device.x < -1.0F ||
          projected.device.x > 1.0F || projected.device.y < -1.0F ||
          projected.device.y > 1.0F) {
        continue;
      }
      const double x = static_cast<double>(point.x) - projection_camera.eye.x;
      const double y = static_cast<double>(point.y) - projection_camera.eye.y;
      const double z = static_cast<double>(point.z) - projection_camera.eye.z;
      const double distance_squared = x * x + y * y + z * z;
      if (distance_squared < nearest_distance_squared) {
        nearest_distance_squared = distance_squared;
        chosen_corner = corner;
      }
    }
  }
  const std::array lengths{
      (static_cast<double>(bounds.max.x) - bounds.min.x) * map_units_per_world_unit,
      (static_cast<double>(bounds.max.y) - bounds.min.y) * map_units_per_world_unit,
      (static_cast<double>(bounds.max.z) - bounds.min.z) * map_units_per_world_unit};
  for (std::size_t axis = 0U; axis < spans.size(); ++axis) {
    if (axis != omitted_axis && std::isfinite(lengths[axis]) &&
        lengths[axis] >= kViewportSelectionDimensionMinimumMapLength) {
      spans[axis] = ViewportSelectionDimensionSpan{
          .start = corner_point(chosen_corner),
          .end = corner_point(chosen_corner ^ (1U << axis))};
    }
  }
  return spans;
}

} // namespace quader

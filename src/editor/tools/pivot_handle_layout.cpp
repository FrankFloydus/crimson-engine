////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/pivot_handle_layout.hpp"

#include "editor/interactions/transform_gizmo.hpp"
#include "editor/interactions/tool_drag_math.hpp"
#include "editor/pivot/pivot_types.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quader {
namespace {

bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float squared_length(QVec2 value) { return value.x * value.x + value.y * value.y; }

float segment_distance_squared(QVec2 point, QVec2 start, QVec2 end) {
  const QVec2 axis = end - start;
  const float squared = squared_length(axis);
  const QVec2 delta = point - start;
  const float fraction = squared > 0.000001f
      ? std::clamp((delta.x * axis.x + delta.y * axis.y) / squared, 0.0f, 1.0f)
      : 0.0f;
  return squared_length(point - (start + axis * fraction));
}

} // namespace

PivotHandleLayout build_pivot_handle_layout(
    const ViewportCameraController &camera, const PivotHandleState &handle,
    float raster_scale, const PivotHandleLayoutOptions &options) {
  PivotHandleLayout layout;
  layout.raster_scale = viewport_sanitized_pixel_scale(raster_scale);
  if (!handle.visible || !finite(handle.world_position)) return layout;
  const auto projected = camera.project_world_point(handle.world_position);
  if (!projected.visible || !std::isfinite(projected.position.x) ||
      !std::isfinite(projected.position.y)) return layout;
  const auto runtime_camera = ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size());
  const auto world_per_pixel_at = [&](QVec3 point) {
    return static_cast<float>(quader_editor_core::viewport_world_units_per_pixel(runtime_camera, point));
  };
  layout.world_units_per_pixel = world_per_pixel_at(handle.world_position);
  if (!std::isfinite(layout.world_units_per_pixel) || layout.world_units_per_pixel <= 0.0f) {
    return layout;
  }
  const auto basis = ViewportCameraAdapter::basis(camera);
  layout.center = handle.world_position;
  layout.center_screen = projected.position;
  layout.camera_right = basis.right;
  layout.camera_up = basis.up;
  const float diameter = std::isfinite(options.icon_size_pixels)
      ? std::clamp(options.icon_size_pixels, 8.0f, 64.0f) : 20.0f;
  layout.icon_radius_pixels = diameter * 0.5f * layout.raster_scale;
  layout.disc_radius_pixels = layout.icon_radius_pixels * 0.48f;
  layout.ring_inner_radius_pixels = layout.icon_radius_pixels * 0.84f;
  layout.persistent = handle.persistent;
  layout.valid = true;
  if (!handle.persistent) return layout;

  constexpr std::array<PivotHandlePart, 3> kParts{
      PivotHandlePart::X, PivotHandlePart::Y, PivotHandlePart::Z};
  const std::array directions{options.world_x, options.world_y, options.world_z};
  constexpr float kAxisLengthPixels = 44.0f;
  for (int axis = 0; axis < 3; ++axis) {
    auto &target = layout.axes[axis];
    target.part = kParts[axis];
    const float magnitude = length(directions[axis]);
    if (!finite(directions[axis]) || !std::isfinite(magnitude) || magnitude <= 0.000001f) continue;
    target.world_direction = directions[axis] / magnitude;
    target.radius_pixels = 5.0f * layout.raster_scale;
    const QVec3 probe = layout.center + target.world_direction *
        (kAxisLengthPixels * layout.raster_scale * layout.world_units_per_pixel);
    const auto projected_probe = camera.project_world_point(probe);
    if (!finite(probe) || !projected_probe.visible) continue;
    const QVec2 probe_axis = projected_probe.position - layout.center_screen;
    const float probe_span = std::sqrt(squared_length(probe_axis));
    // A nearly end-on direction has no stable screen target. Readable axes
    // retain a compact logical length even when perspective foreshortens them.
    if (!std::isfinite(probe_span) || probe_span <= 4.0f * layout.raster_scale) continue;
    const QVec2 desired_tip = layout.center_screen + probe_axis *
        (kAxisLengthPixels * layout.raster_scale / probe_span);
    const auto ray = camera.ray_from_viewport_point(desired_tip);
    const auto distance = quader_editor_core::axis_distance_from_ray(
        target.world_direction, layout.center,
        {.origin = ray.origin, .direction = ray.direction}, 0.000001);
    if (!distance.ok || !std::isfinite(distance.distance) || distance.distance <= 0.0) continue;
    target.world_tip = layout.center + target.world_direction * static_cast<float>(distance.distance);
    const auto tip = camera.project_world_point(target.world_tip);
    if (!finite(target.world_tip) || !tip.visible) continue;
    target.screen_tip = tip.position;
    const QVec2 screen_axis = target.screen_tip - layout.center_screen;
    const float span_squared = squared_length(screen_axis);
    const float minimum_span = layout.icon_radius_pixels + target.radius_pixels +
                               3.0f * layout.raster_scale;
    target.tip_world_units_per_pixel = world_per_pixel_at(target.world_tip);
    target.visible = std::isfinite(span_squared) && span_squared > minimum_span * minimum_span &&
        std::isfinite(target.tip_world_units_per_pixel) && target.tip_world_units_per_pixel > 0.0f;
    if (!target.visible) continue;
    // Trim the visible shaft in screen space so foreshortened axes cannot
    // cross the icon's disc, gap, or ring in either renderer's line pass.
    const QVec2 start_offset = screen_axis / std::sqrt(span_squared) *
        (layout.icon_radius_pixels + 3.0f * layout.raster_scale);
    target.screen_start = layout.center_screen + start_offset;
    target.world_start = layout.center +
        (layout.camera_right * start_offset.x - layout.camera_up * start_offset.y) *
        layout.world_units_per_pixel;
  }
  return layout;
}

PivotHandlePart hit_pivot_handle(const PivotHandleLayout &layout, QVec2 pointer) {
  if (!layout.valid || !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
    return PivotHandlePart::None;
  }
  if (squared_length(pointer - layout.center_screen) <=
      layout.icon_radius_pixels * layout.icon_radius_pixels) {
    return PivotHandlePart::Center;
  }
  PivotHandlePart best = PivotHandlePart::None;
  float best_distance = std::numeric_limits<float>::infinity();
  constexpr float kProjectionTieTolerance = 0.000001f;
  for (const auto &axis : layout.axes) {
    if (!axis.visible || axis.radius_pixels <= 0.0f) continue;
    const float distance = segment_distance_squared(pointer, axis.screen_start, axis.screen_tip) /
                           (axis.radius_pixels * axis.radius_pixels);
    // Axis fills paint in XYZ order. The last control wins a coincident hit;
    // the tiny tolerance also absorbs subpixel projection rounding.
    if (distance <= 1.0f && distance <= best_distance + kProjectionTieTolerance) {
      best_distance = distance;
      best = axis.part;
    }
  }
  return best;
}

} // namespace quader

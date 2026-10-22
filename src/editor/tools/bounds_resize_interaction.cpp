////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/bounds_resize_interaction.hpp"

#include "editor/interactions/tool_drag_math.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quader {
namespace {

bool same(QVec3 a, QVec3 b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

float component(QVec3 value, int axis) {
  return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}

void set_component(QVec3 &value, int axis, float coordinate) {
  if (axis == 0) value.x = coordinate;
  else if (axis == 1) value.y = coordinate;
  else value.z = coordinate;
}

QVec3 basis(const ViewportBoundsResizeState &bounds, int axis) {
  return axis == 0 ? bounds.world_x : axis == 1 ? bounds.world_y : bounds.world_z;
}

QVec3 world_point(const ViewportBoundsResizeState &bounds, QVec3 point) {
  return bounds.world_origin + bounds.world_x * point.x +
         bounds.world_y * point.y + bounds.world_z * point.z;
}

bool same_bounds(const ViewportBoundsResizeState &a,
                 const ViewportBoundsResizeState &b) {
  return a.revision == b.revision && same(a.local_min, b.local_min) &&
         same(a.local_max, b.local_max) && same(a.world_origin, b.world_origin) &&
         same(a.world_x, b.world_x) && same(a.world_y, b.world_y) &&
         same(a.world_z, b.world_z);
}

bool same_camera(const ViewportCameraState &a, const ViewportCameraState &b) {
  return a.projection == b.projection &&
         a.orthographic_size == b.orthographic_size &&
         a.fov_degrees == b.fov_degrees && a.near_plane == b.near_plane &&
         a.far_plane == b.far_plane;
}

bool valid_bounds(const ViewportBoundsResizeState &bounds) {
  if (!finite(bounds.local_min) || !finite(bounds.local_max) ||
      !finite(bounds.world_origin) || !finite(bounds.world_x) ||
      !finite(bounds.world_y) || !finite(bounds.world_z)) {
    return false;
  }
  const double basis_volume = static_cast<double>(length(bounds.world_x)) *
      length(bounds.world_y) * length(bounds.world_z);
  const double determinant = dot(bounds.world_x, cross(bounds.world_y, bounds.world_z));
  return bounds.local_min.x <= bounds.local_max.x &&
         bounds.local_min.y <= bounds.local_max.y &&
         bounds.local_min.z <= bounds.local_max.z &&
         std::isfinite(basis_volume) && basis_volume > 0.0 &&
         std::isfinite(determinant) && std::abs(determinant) > basis_volume * 0.000001;
}

int valid_handle(int handle) { return handle >= 0 && handle < 6 ? handle : -1; }

QVec2 end_on_direction(int axis) {
  constexpr std::array<QVec2, 3> kDirections{
      QVec2{0.70710678f, -0.70710678f}, QVec2{-0.70710678f, -0.70710678f},
      QVec2{0.70710678f, 0.70710678f}};
  return kDirections[axis];
}

} // namespace

const BoundsResizeFrame &BoundsResizeInteraction::project(
    const std::optional<ViewportBoundsResizeState> &bounds,
    const ViewportCameraController &camera, float raster_scale) {
  if (!bounds || !valid_bounds(*bounds)) {
    clear();
    return frame_;
  }
  const float scale = viewport_sanitized_pixel_scale(raster_scale);
  const auto pose = camera.pose();
  const auto size = camera.viewport_size();
  if (cache_valid_ && cached_bounds_ && same_bounds(*bounds, *cached_bounds_) &&
      same_camera(camera.state(), cached_camera_) &&
      same(pose.eye, cached_pose_.eye) && same(pose.target, cached_pose_.target) &&
      same(pose.up, cached_pose_.up) && size.x == cached_viewport_.x &&
      size.y == cached_viewport_.y && frame_.raster_scale == scale) {
    return frame_;
  }
  cached_bounds_ = bounds;
  cached_camera_ = camera.state();
  cached_pose_ = pose;
  cached_viewport_ = size;
  cache_valid_ = true;
  ++projection_count_;
  ++frame_.revision;
  frame_.raster_scale = scale;
  frame_.visible = true;
  const QVec3 center = bounds->local_min * 0.5f + bounds->local_max * 0.5f;
  for (int corner = 0; corner < 8; ++corner) {
    frame_.corners[corner] = world_point(*bounds, {
        corner & 1 ? bounds->local_max.x : bounds->local_min.x,
        corner & 2 ? bounds->local_max.y : bounds->local_min.y,
        corner & 4 ? bounds->local_max.z : bounds->local_min.z});
    if (!finite(frame_.corners[corner])) {
      clear();
      return frame_;
    }
  }
  for (int axis = 0; axis < 3; ++axis) {
    QVec3 minimum = center;
    QVec3 maximum = center;
    set_component(minimum, axis, component(bounds->local_min, axis));
    set_component(maximum, axis, component(bounds->local_max, axis));
    const QVec3 world_minimum = world_point(*bounds, minimum);
    const QVec3 world_maximum = world_point(*bounds, maximum);
    frame_.guide_starts[axis] = world_minimum;
    frame_.guide_ends[axis] = world_maximum;
    const auto projected_minimum = camera.project_world_point(world_minimum);
    const auto projected_maximum = camera.project_world_point(world_maximum);
    const QVec2 projected_axis = projected_maximum.position - projected_minimum.position;
    const float projected_length = std::hypot(projected_axis.x, projected_axis.y);
    const QVec2 direction = projected_length > 4.0f * scale
        ? projected_axis / projected_length : end_on_direction(axis);
    const float world_extent = length(world_maximum - world_minimum);
    const bool enabled = std::isfinite(world_extent) && world_extent > 0.000001f;
    for (int side = 0; side < 2; ++side) {
      const bool positive = side == 0;
      const auto projected = positive ? projected_maximum : projected_minimum;
      frame_.handles[axis * 2 + side] = {
          .axis = axis,
          .positive = positive,
          .world_position = positive ? world_maximum : world_minimum,
          .opposite_world_position = positive ? world_minimum : world_maximum,
          .screen_position = projected.position + direction *
              ((positive ? 1.0f : -1.0f) * 20.0f * scale),
          .depth = projected.depth,
          .radius_pixels = 6.0f * scale,
          .visible = projected.visible && std::isfinite(projected.position.x) &&
                     std::isfinite(projected.position.y),
          .enabled = enabled,
      };
    }
  }
  return frame_;
}

const BoundsResizeFrame &BoundsResizeInteraction::project_presentation(
    const std::optional<ViewportBoundsResizeState> &bounds,
    const ViewportCameraController &camera, float raster_scale,
    const BoundsResizeFrame &interaction_frame) {
  const auto &projected = project(bounds, camera, raster_scale);
  const int active = projected.visible ? valid_handle(interaction_frame.active) : -1;
  const int hovered = projected.visible ? valid_handle(interaction_frame.hovered) : -1;
  if (frame_.active != active || frame_.hovered != hovered) {
    frame_.active = active;
    frame_.hovered = hovered;
    ++frame_.revision;
  }
  return frame_;
}

int BoundsResizeInteraction::hit_test(QVec2 point) const {
  if (!frame_.visible || !std::isfinite(point.x) || !std::isfinite(point.y)) return -1;
  int best = -1;
  float best_distance = std::numeric_limits<float>::infinity();
  float best_depth = std::numeric_limits<float>::infinity();
  for (int index = 0; index < 6; ++index) {
    const auto &handle = frame_.handles[index];
    if (!handle.visible || !handle.enabled || handle.radius_pixels <= 0.0f) continue;
    const QVec2 delta = point - handle.screen_position;
    const float distance = (delta.x * delta.x + delta.y * delta.y) /
                           (handle.radius_pixels * handle.radius_pixels);
    if (distance <= 1.0f && (distance < best_distance ||
        (distance == best_distance && handle.depth < best_depth))) {
      best = index;
      best_distance = distance;
      best_depth = handle.depth;
    }
  }
  return best;
}

void BoundsResizeInteraction::hover(int handle) {
  handle = valid_handle(handle);
  if (frame_.hovered != handle) {
    frame_.hovered = handle;
    ++frame_.revision;
  }
}

void BoundsResizeInteraction::begin(
    int handle, const ViewportBoundsResizeState &bounds,
    const ViewportCameraController &camera, QVec2 press) {
  project(bounds, camera, frame_.raster_scale);
  handle = valid_handle(handle);
  if (handle < 0 || !frame_.visible || !frame_.handles[handle].visible ||
      !frame_.handles[handle].enabled) return;
  frame_.active = handle;
  ++frame_.revision;
  const auto &target = frame_.handles[handle];
  const QVec3 world_axis = basis(bounds, target.axis);
  drag_axis_scale_ = length(world_axis);
  drag_axis_ = world_axis * static_cast<float>(
      (target.positive ? 1.0 : -1.0) / drag_axis_scale_);
  drag_anchor_ = target.world_position;
  drag_press_ = press;
  const auto projected = camera.project_world_point(drag_anchor_);
  const QVec2 offset = target.screen_position - projected.position;
  const float offset_length = std::hypot(offset.x, offset.y);
  drag_fallback_axis_ = offset_length > 0.0001f ? offset / offset_length
                                              : QVec2{0.0f, -1.0f};
  const auto ray = camera.ray_from_viewport_point(press);
  const auto distance = quader_editor_core::axis_distance_from_ray(
      drag_axis_, drag_anchor_, {ray.origin, ray.direction}, 0.0001);
  drag_axis_start_ = distance.ok ? std::optional<double>{distance.distance}
                               : std::nullopt;
  const auto &state = camera.state();
  drag_world_per_pixel_ = state.projection == ViewportProjection::Orthographic
      ? state.orthographic_size / std::max(1.0f, camera.viewport_size().y)
      : 2.0 * std::max(0.0001f, projected.depth) *
          std::tan(state.fov_degrees * 0.008726646259971648) /
          std::max(1.0f, camera.viewport_size().y);
  drag_reference_world_ = std::max(1.0, drag_world_per_pixel_ * 80.0);
  drag_screen_anchor_ = projected.position;
  const auto tip = camera.project_world_point(
      drag_anchor_ + drag_axis_ * static_cast<float>(drag_reference_world_));
  drag_screen_tip_ = tip.position;
  const QVec2 delta = drag_screen_tip_ - drag_screen_anchor_;
  drag_screen_fallback_ = !tip.visible || delta.x * delta.x + delta.y * delta.y < 16.0f ||
      std::abs(dot(drag_axis_, normalize(ray.direction))) > 0.995f;
}

std::optional<double> BoundsResizeInteraction::displacement(
    const ViewportCameraController &camera, QVec2 point) const {
  if (frame_.active < 0 || !std::isfinite(point.x) || !std::isfinite(point.y)) {
    return std::nullopt;
  }
  if (drag_screen_fallback_) {
    const QVec2 delta = point - drag_press_;
    return (delta.x * drag_fallback_axis_.x + delta.y * drag_fallback_axis_.y) *
           drag_world_per_pixel_;
  }
  const auto ray = camera.ray_from_viewport_point(point);
  const auto current = quader_editor_core::axis_distance_from_ray(
      drag_axis_, drag_anchor_, {ray.origin, ray.direction}, 0.0001);
  const auto distance = quader_editor_core::projected_axis_drag_distance({
      .screen_position = point,
      .press_position = drag_press_,
      .screen_axis = drag_screen_tip_ - drag_screen_anchor_,
      .has_axis_origin = drag_axis_start_.has_value(),
      .axis_origin = drag_axis_start_.value_or(0.0),
      .has_current_axis_distance = current.ok,
      .current_axis_distance = current.distance,
      .screen_anchor = drag_screen_anchor_,
      .screen_tip = drag_screen_tip_,
      .reference_distance = drag_reference_world_,
  });
  return distance.ok && std::isfinite(distance.distance)
      ? std::optional<double>{distance.distance} : std::nullopt;
}

void BoundsResizeInteraction::end() {
  if (frame_.active >= 0) {
    frame_.active = -1;
    ++frame_.revision;
  }
}

void BoundsResizeInteraction::clear() {
  if (frame_.visible || frame_.active >= 0 || frame_.hovered >= 0) {
    frame_.visible = false;
    frame_.active = -1;
    frame_.hovered = -1;
    ++frame_.revision;
  }
  cached_bounds_.reset();
  cache_valid_ = false;
}

} // namespace quader

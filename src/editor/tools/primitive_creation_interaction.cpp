////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/primitive_creation_interaction.hpp"

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

bool same_cube(const ViewportPrimitiveCreationState &a,
               const ViewportPrimitiveCreationState &b) {
  return a.object_id == b.object_id && a.revision == b.revision &&
         same(a.local_min, b.local_min) && same(a.local_max, b.local_max) &&
         same(a.world_origin, b.world_origin) && same(a.world_x, b.world_x) &&
         same(a.world_y, b.world_y) && same(a.world_z, b.world_z) &&
         a.subdivisions == b.subdivisions && a.resizing == b.resizing &&
         a.shape == b.shape && a.side_count == b.side_count && a.caps == b.caps &&
         a.radial_segments == b.radial_segments && a.rings == b.rings &&
         a.hemisphere == b.hemisphere && a.major_segments == b.major_segments &&
         a.minor_segments == b.minor_segments && a.tube_radius_ratio == b.tube_radius_ratio &&
         a.sphere_type == b.sphere_type && a.sphere_subdivision == b.sphere_subdivision;
}

bool same_camera(const ViewportCameraState &a, const ViewportCameraState &b) {
  return a.projection == b.projection &&
         a.orthographic_size == b.orthographic_size &&
         a.fov_degrees == b.fov_degrees && a.near_plane == b.near_plane &&
         a.far_plane == b.far_plane;
}

float component(QVec3 v, int axis) {
  return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

void set_component(QVec3 &v, int axis, float value) {
  if (axis == 0) v.x = value;
  else if (axis == 1) v.y = value;
  else v.z = value;
}

QVec3 world_point(const ViewportPrimitiveCreationState &cube, QVec3 local) {
  return cube.world_origin + cube.world_x * local.x +
         cube.world_y * local.y + cube.world_z * local.z;
}

PrimitiveCreationControl project_control(const ViewportCameraController &camera,
                                     QVec3 world, float radius) {
  const ViewportScreenPoint point = camera.project_world_point(world);
  return {.world_position = world,
          .screen_position = point.position,
          .depth = point.depth,
          .radius_pixels = radius,
          .visible = point.visible};
}

QVec3 local_vector(const ViewportPrimitiveCreationState &primitive, QVec3 world) {
  const float determinant = dot(primitive.world_x,
                                cross(primitive.world_y, primitive.world_z));
  if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001f) return {1, 0, 0};
  return {dot(world, cross(primitive.world_y, primitive.world_z)) / determinant,
          dot(world, cross(primitive.world_z, primitive.world_x)) / determinant,
          dot(world, cross(primitive.world_x, primitive.world_y)) / determinant};
}

QVec3 cylinder_radius_axis(const ViewportPrimitiveCreationState &primitive,
                           const ViewportCameraController &camera, QVec3 center) {
  const auto pose = camera.pose();
  QVec3 right = local_vector(primitive, normalize(cross(pose.target - pose.eye, pose.up)));
  QVec3 toward = local_vector(primitive,
      camera.state().projection == ViewportProjection::Orthographic
          ? pose.eye - pose.target : pose.eye - world_point(primitive, center));
  right.y = toward.y = 0;
  right = length(right) > 0.000001f ? normalize(right) : QVec3{1, 0, 0};
  toward = length(toward) > 0.000001f ? normalize(toward) : QVec3{};
  // A front-right side position stays visible and gives the radius drag a
  // useful horizontal screen direction even when looking straight at a side.
  return normalize(right + toward * 0.45f);
}

QVec3 rotate_radial(QVec3 axis, float angle) {
  return {axis.x * std::cos(angle) - axis.z * std::sin(angle), 0,
          axis.x * std::sin(angle) + axis.z * std::cos(angle)};
}

float ellipse_radius(QVec3 radii, QVec3 axis) {
  return static_cast<float>(1.0 / std::hypot(
      static_cast<double>(axis.x) / std::max(0.000001f, radii.x),
      static_cast<double>(axis.z) / std::max(0.000001f, radii.z)));
}

} // namespace

const PrimitiveCreationControlsFrame &PrimitiveCreationInteraction::project(
    const std::optional<ViewportPrimitiveCreationState> &cube,
    const ViewportCameraController &camera, float raster_scale) {
  if (!cube) {
    clear_segment_wheel();
    if (frame_.visible) {
      frame_.visible = false;
      frame_.hovered = -1;
      ++frame_.revision;
    }
    frame_.highlight_point_count = 0;
    frame_.label_key = {};
    cache_valid_ = false;
    return frame_;
  }
  const float scale = viewport_sanitized_pixel_scale(raster_scale);
  const ViewportCameraPose pose = camera.pose();
  const QVec2 size = camera.viewport_size();
  if (cache_valid_ && cached_cube_ && same_cube(*cube, *cached_cube_) &&
      same_camera(camera.state(), cached_camera_) &&
      same(pose.eye, cached_pose_.eye) && same(pose.target, cached_pose_.target) &&
      same(pose.up, cached_pose_.up) && size.x == cached_viewport_.x &&
      size.y == cached_viewport_.y && cached_scale_ == scale) {
    return frame_;
  }
  if (!cached_cube_ || cached_cube_->object_id != cube->object_id ||
      cached_cube_->shape != cube->shape || cached_cube_->sphere_type != cube->sphere_type) {
    clear_segment_wheel();
  }
  cached_cube_ = cube;
  cached_camera_ = camera.state();
  cached_pose_ = pose;
  cached_viewport_ = size;
  cached_scale_ = scale;
  cache_valid_ = true;
  ++projection_count_;
  frame_.visible = true;
  ++frame_.revision;
  const QVec3 center = (cube->local_min + cube->local_max) * 0.5f;
  if (cube->shape != ViewportPrimitiveCreationShape::Box) {
    const bool cone = cube->shape == ViewportPrimitiveCreationShape::Cone;
    const bool sphere = cube->shape == ViewportPrimitiveCreationShape::Sphere;
    const bool torus = cube->shape == ViewportPrimitiveCreationShape::Torus;
    const bool radial_drag = frame_.active == 0 || (torus && frame_.active == 1);
    const QVec3 axis = radial_drag ? frame_.controls[frame_.active].resize_axis
                                  : cylinder_radius_axis(*cube, camera, center);
    const std::array<QVec3, 2> button_anchors = {
        frame_.controls[6].world_position, frame_.controls[7].world_position};
    frame_.controls = {};
    const QVec3 radii = (cube->local_max - cube->local_min) * 0.5f;
    const float radius = ellipse_radius(radii, axis);
    const float ratio = static_cast<float>(cube->tube_radius_ratio);
    const auto dot_control = [&](int index, QVec3 local, QVec3 direction,
                                 ViewportPrimitiveResizeControl resize) {
      frame_.controls[index] = project_control(camera, world_point(*cube, local), 6.0f * scale);
      frame_.controls[index].resize_axis = direction;
      frame_.controls[index].resize_control = resize;
    };
    QVec3 side = center + axis * radius;
    if (cone || (sphere && cube->hemisphere)) side.y = cube->local_min.y;
    dot_control(0, side, axis, torus ? ViewportPrimitiveResizeControl::OuterRadius
                                    : ViewportPrimitiveResizeControl::Radius);
    if (torus) {
      dot_control(1, center + axis * (radius * (1 - 2 * ratio)), axis,
                  ViewportPrimitiveResizeControl::InnerRadius);
    }
    for (const int index : {2, 3}) {
      if (torus && index == 3) continue;
      QVec3 position = torus ? center + axis * (radius * (1 - ratio)) : center;
      position.y = index == 2 ? cube->local_max.y : cube->local_min.y;
      dot_control(index, position, {0, 1, 0}, index == 2
          ? ViewportPrimitiveResizeControl::PositiveY : ViewportPrimitiveResizeControl::NegativeY);
    }
    const auto button = [&](int index, QVec3 local) {
      frame_.controls[index] = project_control(camera, frame_.active == index
          ? button_anchors[index - 6] : world_point(*cube, local), 9.0f * scale);
    };
    const QVec3 button_axis = rotate_radial(axis, -1.2f);
    const QVec3 button_side = button_axis * ellipse_radius(radii, button_axis);
    QVec3 rim = center + ((cone || sphere || torus) ? button_side : axis * radius);
    rim.y = cone || (sphere && cube->hemisphere) ? cube->local_min.y
              : sphere ? center.y : cube->local_max.y;
    if (torus) {
      rim = center + button_side * (1 - ratio);
      rim.y = cube->local_max.y;
    }
    button(6, rim);
    if (sphere && cube->sphere_type == ViewportSphereType::Uv) {
      QVec3 meridian = center + axis * (radius * 0.70710678f);
      meridian.y = cube->hemisphere
          ? cube->local_min.y + 2 * radii.y * 0.70710678f
          : center.y + radii.y * 0.70710678f;
      button(7, meridian);
    } else if (torus) {
      const QVec3 tube_axis = rotate_radial(axis, 1.05f);
      QVec3 tube_top = center + tube_axis * (ellipse_radius(radii, tube_axis) * (1 - ratio));
      tube_top.y = cube->local_max.y;
      button(7, tube_top);
    }
    update_subdivision_controls();
    update_feedback();
    return frame_;
  }
  for (int axis = 0; axis < 3; ++axis) {
    for (int side = 0; side < 2; ++side) {
      QVec3 position = center;
      set_component(position, axis,
                    component(side == 0 ? cube->local_max : cube->local_min,
                              axis));
      frame_.controls[axis * 2 + side] =
          project_control(camera, world_point(*cube, position), 6.0f * scale);
      frame_.controls[axis * 2 + side].resize_axis = {};
      set_component(frame_.controls[axis * 2 + side].resize_axis, axis, 1.0f);
      frame_.controls[axis * 2 + side].resize_control =
          static_cast<ViewportPrimitiveResizeControl>(axis * 2 + side);
    }
    const int index = 6 + axis;
    // Keep the accepted world anchor until release, while refreshing its
    // physical-pixel hit/render target for the current camera and DPI.
    if (frame_.active == index) {
      const QVec3 anchor = frame_.controls[index].world_position;
      frame_.controls[index] = project_control(camera, anchor, 9.0f * scale);
      continue;
    }
    PrimitiveCreationControl best;
    float best_depth = std::numeric_limits<float>::infinity();
    for (int edge = 0; edge < 4; ++edge) {
      QVec3 position = center;
      const int other_a = (axis + 1) % 3;
      const int other_b = (axis + 2) % 3;
      set_component(position, other_a,
                    component((edge & 1) ? cube->local_max : cube->local_min,
                              other_a));
      set_component(position, other_b,
                    component((edge & 2) ? cube->local_max : cube->local_min,
                              other_b));
      PrimitiveCreationControl candidate =
          project_control(camera, world_point(*cube, position), 9.0f * scale);
      if (candidate.visible && candidate.depth < best_depth) {
        best = candidate;
        best_depth = candidate.depth;
      }
    }
    frame_.controls[index] = best;
  }
  update_subdivision_controls();
  update_feedback();
  return frame_;
}

int PrimitiveCreationInteraction::hit_test(QVec2 point) const {
  if (!frame_.visible) return -1;
  int best = -1;
  float best_distance = std::numeric_limits<float>::infinity();
  float best_depth = std::numeric_limits<float>::infinity();
  for (int index = 0; index < 9; ++index) {
    const PrimitiveCreationControl &control = frame_.controls[index];
    if (!control.visible) continue;
    const float dx = point.x - control.screen_position.x;
    const float dy = point.y - control.screen_position.y;
    const float distance = (dx * dx + dy * dy) /
                           (control.radius_pixels * control.radius_pixels);
    if (distance <= 1.0f &&
        (distance < best_distance ||
         (distance == best_distance && control.depth < best_depth))) {
      best = index;
      best_distance = distance;
      best_depth = control.depth;
    }
  }
  return best;
}

void PrimitiveCreationInteraction::hover(int control) {
  if (frame_.hovered != control) {
    frame_.hovered = control;
    update_feedback();
    ++frame_.revision;
  }
}

void PrimitiveCreationInteraction::set_subdivision_decrement(bool decrement) {
  if (subdivision_decrement_ == decrement) return;
  subdivision_decrement_ = decrement;
  update_subdivision_controls();
}

void PrimitiveCreationInteraction::clear_segment_wheel() {
  wheel_control_ = -1;
  wheel_remainder_ = 0.0;
}

std::optional<int> PrimitiveCreationInteraction::wheel_segment_value(
    int control, double steps) {
  if (control < 6 || control >= static_cast<int>(frame_.controls.size()) ||
      !frame_.visible || !frame_.controls[control].visible || !std::isfinite(steps)) {
    clear_segment_wheel();
    return std::nullopt;
  }
  if (wheel_control_ != control) {
    clear_segment_wheel();
    wheel_control_ = control;
  }
  const auto &button = frame_.controls[control];
  if ((steps > 0.0 && button.segment_count >= button.maximum_segments) ||
      (steps < 0.0 && button.segment_count <= button.minimum_segments)) {
    wheel_remainder_ = 0.0;
    return std::nullopt;
  }
  double whole_steps = 0.0;
  if (wheel_remainder_ * steps < 0.0) wheel_remainder_ = 0.0;
  wheel_remainder_ = std::modf(wheel_remainder_ + steps, &whole_steps);
  const int value = static_cast<int>(std::clamp(
      static_cast<double>(button.segment_count) + whole_steps,
      static_cast<double>(button.minimum_segments), static_cast<double>(button.maximum_segments)));
  if ((whole_steps < 0.0 && value == button.minimum_segments) ||
      (whole_steps > 0.0 && value == button.maximum_segments)) wheel_remainder_ = 0.0;
  return value != button.segment_count ? std::optional<int>{value} : std::nullopt;
}

void PrimitiveCreationInteraction::update_subdivision_controls() {
  if (!cached_cube_ || !frame_.visible) return;
  bool changed = false;
  for (int axis = 0; axis < 3; ++axis) {
    const int index = 6 + axis;
    auto &control = frame_.controls[index];
    const bool decrement = frame_.active == index
                               ? active_subdivision_decrement_
                               : subdivision_decrement_;
    int count = cached_cube_->subdivisions[axis];
    int minimum = 1;
    int maximum = 128;
    switch (cached_cube_->shape) {
    case ViewportPrimitiveCreationShape::Cylinder:
    case ViewportPrimitiveCreationShape::Cone: count = cached_cube_->side_count; minimum = 3; break;
    case ViewportPrimitiveCreationShape::Sphere:
      if (cached_cube_->sphere_type == ViewportSphereType::Quad) {
        count = cached_cube_->sphere_subdivision;
        maximum = 6;
      } else {
        count = axis == 0 ? cached_cube_->radial_segments : cached_cube_->rings;
        minimum = axis == 0 ? 3 : 2;
      }
      break;
    case ViewportPrimitiveCreationShape::Torus:
      count = axis == 0 ? cached_cube_->major_segments : cached_cube_->minor_segments;
      minimum = 3;
      break;
    case ViewportPrimitiveCreationShape::Box: break;
    }
    control.segment_count = count;
    control.minimum_segments = minimum;
    control.maximum_segments = maximum;
    const bool enabled = decrement ? count > minimum : count < maximum;
    changed |= control.subdivision_decrement != decrement || control.enabled != enabled;
    control.subdivision_decrement = decrement;
    control.enabled = enabled;
  }
  if (changed) ++frame_.revision;
}

void PrimitiveCreationInteraction::update_feedback() {
  frame_.highlight_point_count = 0;
  frame_.label_key = {};
  const int index = frame_.active >= 0 ? frame_.active : frame_.hovered;
  if (!frame_.visible || !cached_cube_ || index < 0 || index >= 9 ||
      !frame_.controls[index].visible) return;
  const auto &primitive = *cached_cube_;
  const auto &control = frame_.controls[index];
  const QVec3 center = (primitive.local_min + primitive.local_max) * 0.5f;
  const QVec3 radii = (primitive.local_max - primitive.local_min) * 0.5f;
  const bool sphere = primitive.shape == ViewportPrimitiveCreationShape::Sphere;
  const bool torus = primitive.shape == ViewportPrimitiveCreationShape::Torus;
  const bool cone = primitive.shape == ViewportPrimitiveCreationShape::Cone;
  const bool box = primitive.shape == ViewportPrimitiveCreationShape::Box;
  constexpr std::array<std::string_view, 3> kDimensions{
      "primitive.controls.width", "primitive.controls.height", "primitive.controls.depth"};
  frame_.label_position = control.screen_position +
      QVec2{control.radius_pixels + 8 * cached_scale_, -control.radius_pixels};
  if (box) {
    const int axis = index < 6 ? index / 2 : index - 6;
    const QVec3 basis = axis == 0 ? primitive.world_x
                        : axis == 1 ? primitive.world_y : primitive.world_z;
    frame_.label_key = kDimensions[axis];
    frame_.label_value = 2 * component(radii, axis) * length(basis);
    frame_.label_is_count = false;
    if (index >= 6) {
      frame_.highlight_points[0] = control.world_position - basis * component(radii, axis);
      frame_.highlight_points[1] = control.world_position + basis * component(radii, axis);
      frame_.highlight_point_count = 2;
    }
    return;
  }
  frame_.label_is_count = index >= 6;
  if (index < 6) {
    const QVec3 axis = control.resize_axis;
    const float world_scale = length(primitive.world_x * axis.x +
        primitive.world_y * axis.y + primitive.world_z * axis.z);
    if (index == 2 || index == 3) {
      frame_.label_key = torus ? "primitive.controls.tube_height" : "primitive.controls.height";
      frame_.label_value = 2 * radii.y * world_scale;
    } else {
      frame_.label_key = torus ? (index == 0 ? "primitive.controls.outer_radius"
                                           : "primitive.controls.inner_radius")
                               : "primitive.controls.radius";
      frame_.label_value = ellipse_radius(radii, axis) * world_scale *
          (torus && index == 1 ? 1 - 2 * primitive.tube_radius_ratio : 1);
    }
    return;
  }
  frame_.label_value = control.segment_count;
  if (sphere && primitive.sphere_type == ViewportSphereType::Quad) {
    frame_.label_key = "primitive.controls.subdivision";
    return;
  }
  frame_.label_key = sphere ? (index == 6 ? "primitive.controls.radial_segments"
                                         : "primitive.controls.rings")
      : torus ? (index == 6 ? "primitive.controls.major_segments"
                           : "primitive.controls.minor_segments")
              : "primitive.controls.sides";
  QVec3 axis = frame_.controls[0].resize_axis;
  if ((torus || sphere) && index == 7) {
    axis = local_vector(primitive, control.world_position - world_point(primitive, center));
    axis.y = 0;
    axis = length(axis) > 0.000001f ? normalize(axis) : QVec3{1, 0, 0};
  }
  const float radius = ellipse_radius(radii, axis);
  const float ratio = static_cast<float>(primitive.tube_radius_ratio);
  constexpr float kTau = 6.2831853071795864769f;
  for (std::size_t point = 0; point < frame_.highlight_points.size(); ++point) {
    const float fraction = static_cast<float>(point) / 64.0f;
    const float angle = kTau * fraction;
    QVec3 local;
    if (sphere && index == 7) {
      const float latitude = primitive.hemisphere ? angle * 0.25f
                                                  : angle * 0.5f - kTau * 0.25f;
      local = center + axis * (radius * std::cos(latitude));
      local.y = primitive.hemisphere ? primitive.local_min.y + 2 * radii.y * std::sin(latitude)
                                     : center.y + radii.y * std::sin(latitude);
    } else if (torus && index == 7) {
      local = center + axis * (radius * (1 - ratio + ratio * std::cos(angle)));
      local.y += radii.y * std::sin(angle);
    } else {
      const float radial_factor = torus ? 1 - ratio : 1;
      local = center + QVec3{radii.x * radial_factor * std::cos(angle), 0,
                             radii.z * radial_factor * std::sin(angle)};
      local.y = cone || (sphere && primitive.hemisphere) ? primitive.local_min.y
                    : sphere ? center.y : primitive.local_max.y;
    }
    frame_.highlight_points[point] = world_point(primitive, local);
  }
  frame_.highlight_point_count = frame_.highlight_points.size();
}

void PrimitiveCreationInteraction::begin(
    int control, const ViewportPrimitiveCreationState &cube,
    const ViewportCameraController &camera, QVec2 press,
    bool subdivision_decrement) {
  frame_.active = control;
  clear_segment_wheel();
  active_subdivision_decrement_ = subdivision_decrement;
  object_id_ = cube.object_id;
  shape_ = cube.shape;
  sphere_type_ = cube.sphere_type;
  ++frame_.revision;
  update_subdivision_controls();
  update_feedback();
  if (control < 0 || control >= 6) return;
  const QVec3 local_axis = frame_.controls[control].resize_axis;
  const QVec3 basis = cube.world_x * local_axis.x + cube.world_y * local_axis.y +
                      cube.world_z * local_axis.z;
  drag_axis_scale_ = std::max(0.000001, static_cast<double>(length(basis)));
  drag_axis_ = basis / static_cast<float>(drag_axis_scale_);
  drag_anchor_ = frame_.controls[control].world_position;
  drag_press_ = press;
  const ViewportRay ray = camera.ray_from_viewport_point(press);
  const auto distance = quader_editor_core::axis_distance_from_ray(
      drag_axis_, drag_anchor_, {ray.origin, ray.direction}, 0.0001);
  drag_axis_start_ = distance.ok ? std::optional<double>{distance.distance}
                                 : std::nullopt;
  const ViewportCameraState &state = camera.state();
  const auto projected = camera.project_world_point(drag_anchor_);
  drag_world_per_pixel_ =
      state.projection == ViewportProjection::Orthographic
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
  drag_screen_fallback_ = !tip.visible ||
      delta.x * delta.x + delta.y * delta.y < 16.0f ||
      std::abs(dot(drag_axis_, normalize(ray.direction))) > 0.995f;
}

std::optional<double> PrimitiveCreationInteraction::displacement(
    const ViewportCameraController &camera, QVec2 point) const {
  if (frame_.active < 0 || frame_.active >= 6) return std::nullopt;
  if (drag_screen_fallback_) {
    const auto control = frame_.controls[frame_.active].resize_control;
    const double sign = control == ViewportPrimitiveResizeControl::NegativeX ||
        control == ViewportPrimitiveResizeControl::NegativeY ||
        control == ViewportPrimitiveResizeControl::NegativeZ ? -1.0 : 1.0;
    return (drag_press_.y - point.y) * drag_world_per_pixel_ * sign /
           drag_axis_scale_;
  }
  const ViewportRay ray = camera.ray_from_viewport_point(point);
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
  return distance.ok ? std::optional<double>{distance.distance / drag_axis_scale_}
                     : std::nullopt;
}

ViewportPointerCursor
PrimitiveCreationInteraction::pointer_cursor(bool drag_started) const {
  if (!frame_.visible) return ViewportPointerCursor::Default;
  const int control = frame_.active >= 0 ? frame_.active : frame_.hovered;
  if (control < 0 || control >= 6 ||
      !frame_.controls[control].visible || !frame_.controls[control].enabled) {
    return ViewportPointerCursor::Default;
  }
  return frame_.active >= 0 && frame_.active < 6 && drag_started
             ? ViewportPointerCursor::ClosedHand
             : ViewportPointerCursor::OpenHand;
}

void PrimitiveCreationInteraction::end() {
  if (frame_.active != -1) {
    frame_.active = -1;
    update_subdivision_controls();
    update_feedback();
    ++frame_.revision;
    cache_valid_ = false;
  }
}

void PrimitiveCreationInteraction::clear() {
  clear_segment_wheel();
  end();
  if (frame_.visible || frame_.hovered != -1) {
    frame_.visible = false;
    frame_.hovered = -1;
    frame_.highlight_point_count = 0;
    frame_.label_key = {};
    ++frame_.revision;
  }
  cached_cube_.reset();
  cache_valid_ = false;
}

} // namespace quader

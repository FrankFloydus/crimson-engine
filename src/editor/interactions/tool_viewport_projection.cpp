////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/viewport_projection.hpp>
#include "editor/interactions/viewport_segment_projection.hpp"

#include "tool_math_internal.hpp"

#include <editor/interactions/tool_snapping.hpp>

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <cmath>

namespace quader_editor_core {
namespace {

quader_geometry::QVec3d to_geometry_vec3(quader::QVec3 value)
{
	return { value.x, value.y, value.z };
}

quader::QVec3 to_poly_vec3(quader_geometry::QVec3d value)
{
	return { static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z) };
}

quader_geometry::QLine3<double> to_geometry_line(quader_poly::Ray ray)
{
	return { to_geometry_vec3(ray.origin), to_geometry_vec3(ray.direction) };
}

quader_geometry::QPlane3<double> to_geometry_plane(quader::QVec3 origin, quader::QVec3 normal)
{
  return quader_geometry::plane_from_point_normal(
      to_geometry_vec3(origin), to_geometry_vec3(normal), kToolEpsilon);
}

quader::QVec3 project_poly_point_to_plane(quader::QVec3 point, const quader_geometry::QPlane3<double>& plane)
{
	return to_poly_vec3(quader_geometry::project_point_to_plane(to_geometry_vec3(point), plane));
}

quader::QVec3 camera_axis(quader::QVec3 axis, quader::QVec3 fallback,
		bool affine_model_space)
{
	if (vec3_is_tiny(axis)) {
		return fallback;
	}
	return affine_model_space ? axis : normalized_axis(axis);
}

} // namespace

PreparedViewportProjection::PreparedViewportProjection(
    const ViewportCameraState &camera)
    : projection_(camera.projection),
      affine_model_space_(camera.affine_model_space),
      origin_(camera.transform.origin),
      right_(camera_axis(camera.transform.x_axis, {1.0F, 0.0F, 0.0F},
                         camera.affine_model_space)),
      up_(camera_axis(camera.transform.y_axis, {0.0F, 1.0F, 0.0F},
                      camera.affine_model_space)),
      forward_(camera_axis(vec3_scale(camera.transform.z_axis, -1.0),
                           {0.0F, 0.0F, -1.0F}, camera.affine_model_space)),
      width_(std::max(1.0, static_cast<double>(camera.viewport_size.x))),
      height_(std::max(1.0, static_cast<double>(camera.viewport_size.y))),
      aspect_(width_ / height_) {
  if (projection_ == CameraProjection::Orthographic) {
    half_height_ = std::max(0.0001, camera.orthographic_size * 0.5);
  } else {
    const double fov = std::clamp(camera.fov_degrees, 1.0, 179.0) *
                       std::acos(-1.0) / 180.0;
    tangent_ = std::tan(fov * 0.5);
  }
  if (affine_model_space_) {
    up_cross_forward_ = quader::cross(up_, forward_);
    determinant_ = quader::dot(right_, up_cross_forward_);
    constexpr float kAffineDeterminantEpsilon = 1.0e-12F;
    affine_basis_valid_ = std::isfinite(determinant_) &&
                         std::abs(determinant_) > kAffineDeterminantEpsilon;
    if (affine_basis_valid_) {
      forward_cross_right_ = quader::cross(forward_, right_);
      right_cross_up_ = quader::cross(right_, up_);
    }
    if (projection_ == CameraProjection::Orthographic) {
      orthographic_affine_direction_ =
          normalize(vector_from_camera_components({0.0F, 0.0F, 1.0F}));
    }
  }
}

quader::QVec3 PreparedViewportProjection::vector_from_camera_components(
    quader::QVec3 components) const {
  if (!affine_basis_valid_) {
    return {};
  }
  return (up_cross_forward_ * components.x +
          forward_cross_right_ * components.y +
          right_cross_up_ * components.z) /
         determinant_;
}

bool PreparedViewportProjection::project_point(
    quader::QVec3 point, quader::QVec2 &out_screen) const {
  const quader::QVec3 local = point - origin_;
  const double view_distance = vec3_dot(local, forward_);
  if (projection_ == CameraProjection::Orthographic) {
    const double ndc_x = vec3_dot(local, right_) / (half_height_ * aspect_);
    const double ndc_y = vec3_dot(local, up_) / half_height_;
    out_screen = {
        static_cast<float>((ndc_x + 1.0) * 0.5 * width_),
        static_cast<float>((1.0 - ndc_y) * 0.5 * height_),
    };
    return std::isfinite(out_screen.x) && std::isfinite(out_screen.y);
  }
  if (view_distance <= 0.0001) {
    return false;
  }
  const double ndc_x =
      vec3_dot(local, right_) / (view_distance * tangent_ * aspect_);
  const double ndc_y = vec3_dot(local, up_) / (view_distance * tangent_);
  out_screen = {
      static_cast<float>((ndc_x + 1.0) * 0.5 * width_),
      static_cast<float>((1.0 - ndc_y) * 0.5 * height_),
  };
  return std::isfinite(out_screen.x) && std::isfinite(out_screen.y);
}

bool PreparedViewportProjection::point_is_behind_camera(
    quader::QVec3 point) const {
  return projection_ != CameraProjection::Orthographic &&
         vec3_dot(point - origin_, forward_) <= 0.0001F;
}

quader_poly::Ray PreparedViewportProjection::ray_from_screen_point(
    quader::QVec2 screen) const {
  const double ndc_x = (static_cast<double>(screen.x) / width_) * 2.0 - 1.0;
  const double ndc_y = 1.0 - (static_cast<double>(screen.y) / height_) * 2.0;
  if (projection_ == CameraProjection::Orthographic) {
    if (affine_model_space_) {
      const quader::QVec3 offset = vector_from_camera_components(
          {static_cast<float>(ndc_x * half_height_ * aspect_),
           static_cast<float>(ndc_y * half_height_), 0.0F});
      return {origin_ + offset, orthographic_affine_direction_};
    }
    return {origin_ + vec3_scale(right_, ndc_x * half_height_ * aspect_) +
                vec3_scale(up_, ndc_y * half_height_),
            forward_};
  }
  quader::QVec3 direction = affine_model_space_
      ? vector_from_camera_components(
            {static_cast<float>(ndc_x * tangent_ * aspect_),
             static_cast<float>(ndc_y * tangent_), 1.0F})
      : forward_ + vec3_scale(right_, ndc_x * tangent_ * aspect_) +
            vec3_scale(up_, ndc_y * tangent_);
  direction = vec3_is_tiny(direction) ? forward_ : normalized_axis(direction);
  return {origin_, direction};
}

bool project_viewport_point(const ViewportCameraState &camera,
                            quader::QVec3 point, quader::QVec2 &out_screen) {
  return PreparedViewportProjection(camera).project_point(point, out_screen);
}

bool viewport_point_is_behind_camera(const ViewportCameraState &camera,
                                     quader::QVec3 point) {
  if (camera.projection == CameraProjection::Orthographic) {
    return false;
  }
  const quader::QVec3 forward = camera_axis(
      vec3_scale(camera.transform.z_axis, -1.0), {0.0F, 0.0F, -1.0F},
      camera.affine_model_space);
  return vec3_dot(point - camera.transform.origin, forward) <= 0.0001F;
}

quader_poly::Ray ray_from_viewport_screen_point(
    const ViewportCameraState &camera, quader::QVec2 screen) {
  return PreparedViewportProjection(camera).ray_from_screen_point(screen);
}

quader::QVec3 cut_grid_plane_normal_for_camera(const ViewportCameraState& camera)
{
  if (camera.projection != CameraProjection::Orthographic) {
    return {0.0F, 1.0F, 0.0F};
  }

        const quader::QVec3 forward = vec3_is_tiny(camera.transform.z_axis) ?
		quader::QVec3 { 0.0F, 0.0F, -1.0F } :
		normalized_axis(vec3_scale(camera.transform.z_axis, -1.0));
	const float abs_x = std::abs(forward.x);
	const float abs_y = std::abs(forward.y);
	const float abs_z = std::abs(forward.z);
	if (abs_x >= abs_y && abs_x >= abs_z) {
		return { 1.0F, 0.0F, 0.0F };
	}
	if (abs_z >= abs_x && abs_z >= abs_y) {
		return { 0.0F, 0.0F, 1.0F };
	}
	return { 0.0F, 1.0F, 0.0F };
}

CutGridPointResult viewport_grid_point_from_ray(const ViewportCameraState& camera, quader_poly::Ray ray, double grid_size, bool snap_enabled)
{
	const quader::QVec3 plane_normal = cut_grid_plane_normal_for_camera(camera);
	if (vec3_is_tiny(ray.direction)) {
		return { false, "Invalid viewport ray.", {}, plane_normal };
	}

	const quader_geometry::QPlane3<double> plane = to_geometry_plane({}, plane_normal);
        const quader_geometry::QLinePlaneHit<double> hit =
            quader_geometry::intersect_line_plane(to_geometry_line(ray), plane,
                                                  kToolEpsilon);
        quader::QVec3 point;
        if (hit.type == quader_geometry::QPlaneIntersectionType::Point) {
          point = to_poly_vec3(
              quader_geometry::project_point_to_plane(hit.point, plane));
        } else {
          point = project_poly_point_to_plane(ray.origin, plane);
        }

        return { true, {}, snap_knife_point_to_grid(point, grid_size, snap_enabled), plane_normal };
}

CutGridPointResult cut_grid_point_from_ray(const ViewportCameraState& camera, quader_poly::Ray ray, double grid_size)
{
	CutGridPointResult result = viewport_grid_point_from_ray(camera, ray, grid_size, true);
	if (!result.ok && result.message == "Invalid viewport ray.") {
		result.message = "Invalid cut ray.";
	}
	return result;
}

CutGridPointResult cut_support_plane_point_from_ray(
    quader_poly::Ray ray, quader::QVec3 plane_origin,
    quader::QVec3 plane_normal, double grid_size)
{
  if (vec3_is_tiny(ray.direction) || vec3_is_tiny(plane_normal)) {
    return {false, "Invalid cut ray or support plane."};
  }
  plane_normal = normalized_axis(plane_normal);
  const auto plane = to_geometry_plane(plane_origin, plane_normal);
  const auto hit = quader_geometry::intersect_line_plane(
      to_geometry_line(ray), plane, kToolEpsilon);
  const quader::QVec3 point =
      hit.type == quader_geometry::QPlaneIntersectionType::Point
      ? to_poly_vec3(hit.point)
      : project_poly_point_to_plane(ray.origin, plane);
  // World-grid quantization must not move a point off its retained surface.
  const quader::QVec3 snapped = snap_knife_point_to_grid(point, grid_size, true);
  return {true, {}, project_poly_point_to_plane(snapped, plane), plane_normal};
}
} // namespace quader_editor_core

namespace quader {
namespace {

/// Intersects a segment's retained parameter interval with one axis-aligned slab.
bool clip_segment_axis(double start, double end, double minimum, double maximum,
                       double &first, double &last) {
  if (!std::isfinite(start) || !std::isfinite(end)) {
    return false;
  }
  const double delta = end - start;
  if (delta == 0.0) {
    return start >= minimum && start <= maximum;
  }
  double enter = (minimum - start) / delta;
  double leave = (maximum - start) / delta;
  if (enter > leave) {
    std::swap(enter, leave);
  }
  first = std::max(first, enter);
  last = std::min(last, leave);
  return first <= last;
}

} // namespace

std::optional<ViewportScreenSegment> project_visible_viewport_segment(
    quader_editor_core::ViewportCameraState camera, QVec3 start, QVec3 end,
    double near_plane, double far_plane, bool clip_to_viewport) {
  const float viewport_width = camera.viewport_size.x;
  const float viewport_height = camera.viewport_size.y;
  if (!std::isfinite(viewport_width) || !std::isfinite(viewport_height) ||
      !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
      near_plane > far_plane) {
    return std::nullopt;
  }
  const QVec3 eye = camera.transform.origin;
  const QVec3 forward = quader_editor_core::camera_axis(
      camera.transform.z_axis * -1.0f, {0.0f, 0.0f, -1.0f},
      camera.affine_model_space);
  const auto depth = [forward, eye](QVec3 point) {
    return (static_cast<double>(point.x) - eye.x) * forward.x +
           (static_cast<double>(point.y) - eye.y) * forward.y +
           (static_cast<double>(point.z) - eye.z) * forward.z;
  };
  double first = 0.0;
  double last = 1.0;
  if (!clip_segment_axis(depth(start), depth(end), near_plane, far_plane,
                         first, last)) {
    return std::nullopt;
  }
  const double depth_clipped_first = first;
  const double depth_clipped_last = last;
  const double start_depth = depth(start);
  const double end_depth = depth(end);
  const double clipped_start_depth = std::lerp(start_depth, end_depth, first);
  const double clipped_end_depth = std::lerp(start_depth, end_depth, last);
  // Interpolate relative to the eye: rounding a clipped point back into large
  // absolute float coordinates can erase its small positive near-plane depth.
  camera.transform.origin = {};
  const auto relative_point = [start, end, eye](double parameter) -> QVec3 {
    return {
        static_cast<float>(std::lerp(static_cast<double>(start.x) - eye.x,
                                     static_cast<double>(end.x) - eye.x,
                                     parameter)),
        static_cast<float>(std::lerp(static_cast<double>(start.y) - eye.y,
                                     static_cast<double>(end.y) - eye.y,
                                     parameter)),
        static_cast<float>(std::lerp(static_cast<double>(start.z) - eye.z,
                                     static_cast<double>(end.z) - eye.z,
                                     parameter)),
    };
  };
  ViewportScreenSegment projected;
  const quader_editor_core::PreparedViewportProjection projection(camera);
  // Clip depth before perspective division, so an endpoint behind the eye
  // cannot mirror the line. Exact near/far endpoints still project correctly;
  // project_world_point().visible deliberately excludes those boundaries.
  if (!projection.project_point(relative_point(first), projected.start) ||
      !projection.project_point(relative_point(last), projected.end)) {
    return std::nullopt;
  }
  if (!clip_to_viewport) {
    projected.start_parameter = depth_clipped_first;
    projected.end_parameter = depth_clipped_last;
    projected.start_depth = clipped_start_depth;
    projected.end_depth = clipped_end_depth;
    return projected;
  }
  first = 0.0;
  last = 1.0;
  if (!clip_segment_axis(projected.start.x, projected.end.x, 0.0,
                         viewport_width, first, last) ||
      !clip_segment_axis(projected.start.y, projected.end.y, 0.0,
                         viewport_height, first, last)) {
    return std::nullopt;
  }
  const auto screen_point = [&](double parameter) -> QVec2 {
    return {
        static_cast<float>(std::clamp(
            std::lerp(static_cast<double>(projected.start.x),
                      static_cast<double>(projected.end.x), parameter),
            0.0, static_cast<double>(viewport_width))),
        static_cast<float>(std::clamp(
            std::lerp(static_cast<double>(projected.start.y),
                      static_cast<double>(projected.end.y), parameter),
            0.0, static_cast<double>(viewport_height))),
    };
  };
  const auto world_parameter = [&](double screen_parameter) {
    const double parameter =
        camera.projection == quader_editor_core::CameraProjection::Orthographic
        ? screen_parameter
        : screen_parameter * clipped_start_depth /
              std::lerp(clipped_end_depth, clipped_start_depth, screen_parameter);
    return std::lerp(depth_clipped_first, depth_clipped_last, parameter);
  };
  const double world_first = world_parameter(first);
  const double world_last = world_parameter(last);
  return ViewportScreenSegment{
      .start = screen_point(first),
      .end = screen_point(last),
      .start_parameter = world_first,
      .end_parameter = world_last,
      .start_depth = std::lerp(start_depth, end_depth, world_first),
      .end_depth = std::lerp(start_depth, end_depth, world_last),
  };
}

} // namespace quader

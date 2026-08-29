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

quader::QVec3 vector_from_camera_components(
		quader::QVec3 right,
		quader::QVec3 up,
		quader::QVec3 forward,
		quader::QVec3 components)
{
	const quader::QVec3 up_cross_forward = quader::cross(up, forward);
	const float determinant = quader::dot(right, up_cross_forward);
	constexpr float kAffineDeterminantEpsilon = 1.0e-12F;
	if (!std::isfinite(determinant) ||
		std::abs(determinant) <= kAffineDeterminantEpsilon) {
		return {};
	}
	return (up_cross_forward * components.x +
		quader::cross(forward, right) * components.y +
		quader::cross(right, up) * components.z) /
		determinant;
}

} // namespace

bool project_viewport_point(const ViewportCameraState& camera, quader::QVec3 point, quader::QVec2& out_screen)
{
	const quader::QVec3 origin = camera.transform.origin;
	const quader::QVec3 right = camera_axis(camera.transform.x_axis,
		{1.0F, 0.0F, 0.0F}, camera.affine_model_space);
	const quader::QVec3 up = camera_axis(camera.transform.y_axis,
		{0.0F, 1.0F, 0.0F}, camera.affine_model_space);
	const quader::QVec3 forward = camera_axis(
		vec3_scale(camera.transform.z_axis, -1.0),
		{0.0F, 0.0F, -1.0F}, camera.affine_model_space);
	const double width = std::max(1.0, static_cast<double>(camera.viewport_size.x));
	const double height = std::max(1.0, static_cast<double>(camera.viewport_size.y));
	const double aspect = width / height;
	const quader::QVec3 local = point - origin;
	const double view_distance = vec3_dot(local, forward);
        if (camera.projection == CameraProjection::Orthographic) {
          const double half_height =
              std::max(0.0001, camera.orthographic_size * 0.5);
          const double ndc_x = vec3_dot(local, right) / (half_height * aspect);
          const double ndc_y = vec3_dot(local, up) / half_height;
          out_screen = {
              static_cast<float>((ndc_x + 1.0) * 0.5 * width),
              static_cast<float>((1.0 - ndc_y) * 0.5 * height),
          };
          return std::isfinite(out_screen.x) && std::isfinite(out_screen.y);
        }

        if (view_distance <= 0.0001) {
		return false;
	}
	const double fov = std::clamp(camera.fov_degrees, 1.0, 179.0) * std::acos(-1.0) / 180.0;
	const double tangent = std::tan(fov * 0.5);
	const double ndc_x = vec3_dot(local, right) / (view_distance * tangent * aspect);
	const double ndc_y = vec3_dot(local, up) / (view_distance * tangent);
	out_screen = {
		static_cast<float>((ndc_x + 1.0) * 0.5 * width),
		static_cast<float>((1.0 - ndc_y) * 0.5 * height),
	};
	return std::isfinite(out_screen.x) && std::isfinite(out_screen.y);
}

bool viewport_point_is_behind_camera(const ViewportCameraState& camera, quader::QVec3 point)
{
  if (camera.projection == CameraProjection::Orthographic) {
    return false;
  }
	const quader::QVec3 forward = camera_axis(
		vec3_scale(camera.transform.z_axis, -1.0),
		{0.0F, 0.0F, -1.0F}, camera.affine_model_space);
	return vec3_dot(point - camera.transform.origin, forward) <= 0.0001F;
}

quader_poly::Ray ray_from_viewport_screen_point(const ViewportCameraState& camera, quader::QVec2 screen)
{
	const quader::QVec3 origin = camera.transform.origin;
	const quader::QVec3 right = camera_axis(camera.transform.x_axis,
		{1.0F, 0.0F, 0.0F}, camera.affine_model_space);
	const quader::QVec3 up = camera_axis(camera.transform.y_axis,
		{0.0F, 1.0F, 0.0F}, camera.affine_model_space);
	const quader::QVec3 forward = camera_axis(
		vec3_scale(camera.transform.z_axis, -1.0),
		{0.0F, 0.0F, -1.0F}, camera.affine_model_space);
	const double width = std::max(1.0, static_cast<double>(camera.viewport_size.x));
	const double height = std::max(1.0, static_cast<double>(camera.viewport_size.y));
	const double aspect = width / height;
	const double ndc_x = (static_cast<double>(screen.x) / width) * 2.0 - 1.0;
	const double ndc_y = 1.0 - (static_cast<double>(screen.y) / height) * 2.0;
        if (camera.projection == CameraProjection::Orthographic) {
          const double half_height =
              std::max(0.0001, camera.orthographic_size * 0.5);
		  if (camera.affine_model_space) {
			const quader::QVec3 offset = vector_from_camera_components(
				right, up, forward,
				{static_cast<float>(ndc_x * half_height * aspect),
				 static_cast<float>(ndc_y * half_height), 0.0F});
			const quader::QVec3 direction = normalize(
				vector_from_camera_components(right, up, forward,
					{0.0F, 0.0F, 1.0F}));
			return {origin + offset, direction};
		  }
          return {origin + vec3_scale(right, ndc_x * half_height * aspect) +
                      vec3_scale(up, ndc_y * half_height),
                  forward};
        }

        const double fov = std::clamp(camera.fov_degrees, 1.0, 179.0) * std::acos(-1.0) / 180.0;
	const double tangent = std::tan(fov * 0.5);
	quader::QVec3 direction = camera.affine_model_space
		? vector_from_camera_components(
			right, up, forward,
			{static_cast<float>(ndc_x * tangent * aspect),
			 static_cast<float>(ndc_y * tangent), 1.0F})
		: forward + vec3_scale(right, ndc_x * tangent * aspect) +
			vec3_scale(up, ndc_y * tangent);
	direction = vec3_is_tiny(direction) ? forward : normalized_axis(direction);
	return { origin, direction };
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
  // Clip depth before perspective division, so an endpoint behind the eye
  // cannot mirror the line. Exact near/far endpoints still project correctly;
  // project_world_point().visible deliberately excludes those boundaries.
  if (!quader_editor_core::project_viewport_point(camera, relative_point(first),
                                                  projected.start) ||
      !quader_editor_core::project_viewport_point(camera, relative_point(last),
                                                  projected.end)) {
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

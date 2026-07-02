////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/tool_drag_math.hpp>

#include "tool_math_internal.hpp"

#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

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

quader_geometry::QRay3<double> to_geometry_ray(quader_poly::Ray ray)
{
	return { to_geometry_vec3(ray.origin), to_geometry_vec3(ray.direction) };
}

quader_geometry::QPlane3<double> to_geometry_plane(quader::QVec3 origin, quader::QVec3 normal)
{
  return quader_geometry::plane_from_point_normal(
      to_geometry_vec3(origin), to_geometry_vec3(normal), kToolEpsilon);
}

std::optional<double> geometry_axis_drag_distance(
    quader::QVec3 axis_value, quader::QVec3 anchor, const quader_poly::Ray &ray,
    double parallel_epsilon, double vector_epsilon = kToolEpsilon) {
  if (vec3_is_tiny(axis_value, vector_epsilon) ||
      vec3_is_tiny(ray.direction, vector_epsilon)) {
    return std::nullopt;
  }
  const quader_geometry::QVec3d axis = quader_geometry::normalize_or_zero(
      to_geometry_vec3(axis_value), vector_epsilon);
  const quader_geometry::QVec3d ray_direction =
      quader_geometry::normalize_or_zero(to_geometry_vec3(ray.direction),
                                         vector_epsilon);
  const double b = quader_geometry::dot(axis, ray_direction);
  const double denominator = 1.0 - b * b;
  if (std::abs(denominator) <= parallel_epsilon) {
    return std::nullopt;
  }
  const quader_geometry::QLineLineClosestPoints<double> closest =
      quader_geometry::closest_points_line_line(
          quader_geometry::QLine3<double>{to_geometry_vec3(anchor), axis},
          quader_geometry::QLine3<double>{to_geometry_vec3(ray.origin),
                                          ray_direction},
          vector_epsilon);
  return closest.valid ? std::optional<double>{closest.first_parameter}
                       : std::nullopt;
}

bool ray_plane_intersection(quader_poly::Ray ray, quader::QVec3 plane_point, quader::QVec3 plane_normal, quader::QVec3& out_point)
{
	const quader::QVec3 normal = vec3_is_tiny(plane_normal) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(plane_normal);
	const quader::QVec3 direction = vec3_is_tiny(ray.direction) ? quader::QVec3 { 0.0F, 0.0F, 1.0F } : normalized_axis(ray.direction);
	const quader_geometry::QPlane3<double> plane = to_geometry_plane(plane_point, normal);
        const quader_geometry::QRayPlaneHit<double> hit =
            quader_geometry::intersect_ray_plane(
                to_geometry_ray({ray.origin, direction}), plane, kToolEpsilon);
        if (!hit.hit) {
		return false;
	}
	out_point = to_poly_vec3(hit.point);
	return true;
}

} // namespace

double drag_world_units_per_pixel(const CameraDragState& camera, quader::QVec3 anchor)
{
	if (!camera.valid) {
		return 1.0;
	}
	const double viewport_height = std::max(1.0, static_cast<double>(camera.viewport_size.y));
        if (camera.projection == CameraProjection::Orthographic) {
          return std::max(0.0001, camera.orthographic_size / viewport_height);
        }
        const quader::QVec3 to_anchor = anchor - camera.origin;
	const double distance = std::max(0.1, std::abs(vec3_dot(to_anchor, camera.forward)));
	const double clamped_fov = std::clamp(camera.fov_degrees, 1.0, 179.0);
	const double fov_radians = clamped_fov * std::acos(-1.0) / 180.0;
	return std::max(0.0001, 2.0 * distance * std::tan(fov_radians * 0.5) / viewport_height);
}

DragPointResult drag_point_on_plane(const quader_poly::Ray& ray, quader::QVec3 plane_point, quader::QVec3 plane_normal)
{
	quader::QVec3 point;
	if (!ray_plane_intersection(ray, plane_point, plane_normal, point)) {
		return {};
	}
	return { true, point };
}

DragPointResult horizontal_drag_point(const quader_poly::Ray& ray, quader::QVec3 anchor)
{
	if (vec3_is_tiny(ray.direction)) {
		return {};
	}
	DragPointResult result = drag_point_on_plane(ray, anchor, { 0.0F, 1.0F, 0.0F });
	if (!result.ok) {
		return {};
	}
	quader::QVec3 point = result.point;
	point.y = anchor.y;
	return { true, point };
}

DragDistanceResult vertical_drag_value(const quader_poly::Ray& ray, quader::QVec3 anchor)
{
  const std::optional<double> distance = geometry_axis_drag_distance(
      {0.0F, 1.0F, 0.0F}, anchor, ray, kToolEpsilon);
  if (!distance.has_value()) {
    return {};
  }
	return { true, anchor.y + *distance };
}

quader::QVec3 screen_fallback_move_delta(const CameraDragState& camera, quader::QVec2 screen_position, quader::QVec2 press_position, quader::QVec3 anchor, bool vertical)
{
	if (!camera.valid) {
		return {};
	}
	const double units_per_pixel = drag_world_units_per_pixel(camera, anchor);
	const quader::QVec2 screen_delta = vec2_sub(screen_position, press_position);
	quader::QVec3 right = normalized_axis(camera.right);
	quader::QVec3 up = normalized_axis(camera.up);
	if (vec3_is_tiny(right)) {
		right = { 1.0F, 0.0F, 0.0F };
	}
	if (vec3_is_tiny(up)) {
		up = { 0.0F, 1.0F, 0.0F };
	}
	const quader::QVec3 world_delta =
			vec3_scale(right, static_cast<double>(screen_delta.x) * units_per_pixel) -
			vec3_scale(up, static_cast<double>(screen_delta.y) * units_per_pixel);
	if (vertical) {
          if (std::abs(world_delta.y) > kToolEpsilon) {
            return {0.0F, world_delta.y, 0.0F};
          }
                return { 0.0F, static_cast<float>(-screen_delta.y * units_per_pixel), 0.0F };
	}
	return { world_delta.x, 0.0F, world_delta.z };
}

quader::QVec3 snapped_move_delta(quader::QVec3 delta, bool vertical, double grid_world_size)
{
  const double grid_size = safe_grid_size(grid_world_size, kToolEpsilon);
  const auto snap_component = [grid_size](double value) {
    return static_cast<float>(std::round(value / grid_size) * grid_size);
  };
  if (vertical) {
    return {0.0F, snap_component(delta.y), 0.0F};
  }
	return { snap_component(delta.x), 0.0F, snap_component(delta.z) };
}

DragDistanceResult axis_distance_from_ray(
		quader::QVec3 world_axis,
		quader::QVec3 anchor,
		const quader_poly::Ray& ray,
		double parallel_epsilon,
		double vector_epsilon)
{
	const std::optional<double> distance =
			geometry_axis_drag_distance(world_axis, anchor, ray, parallel_epsilon, vector_epsilon);
	if (!distance.has_value()) {
		return {};
	}
	if (!std::isfinite(*distance)) {
		return {};
	}
	return { true, *distance };
}

DragDistanceResult projected_axis_drag_distance(const ProjectedAxisDragInput& input)
{
	if (!input.uses_screen_handle && input.has_axis_origin && input.has_current_axis_distance) {
		const double distance = input.current_axis_distance - input.axis_origin;
		if (std::isfinite(distance)) {
			return { true, distance };
		}
	}

	quader::QVec2 screen_axis = vec2_normalize_or(input.screen_axis, { 1.0F, 0.0F });
	if (input.anchor_behind || input.tip_behind) {
		return {};
	}
	const double projected_pixels = vec2_dot(vec2_sub(input.screen_tip, input.screen_anchor), screen_axis);
        if (std::abs(projected_pixels) <= kToolEpsilon) {
          return {};
        }

        const double pixels = vec2_dot(vec2_sub(input.screen_position, input.press_position), screen_axis);
	const double distance = pixels * input.reference_distance / projected_pixels;
	if (!std::isfinite(distance)) {
		return {};
	}
	return { true, distance };
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/viewport_projection.hpp>

#include "tool_math_internal.hpp"

#include <editor/interactions/tool_snapping.hpp>

#include <mesh/geometry/geometry.hpp>

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
	return quader_geometry::plane_from_point_normal(to_geometry_vec3(origin), to_geometry_vec3(normal), tool_epsilon);
}

quader::QVec3 project_poly_point_to_plane(quader::QVec3 point, const quader_geometry::QPlane3<double>& plane)
{
	return to_poly_vec3(quader_geometry::project_point_to_plane(to_geometry_vec3(point), plane));
}

} // namespace

bool project_viewport_point(const ViewportCameraState& camera, quader::QVec3 point, quader::QVec2& out_screen)
{
	const quader::QVec3 origin = camera.transform.origin;
	const quader::QVec3 right = vec3_is_tiny(camera.transform.x_axis) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : normalized_axis(camera.transform.x_axis);
	const quader::QVec3 up = vec3_is_tiny(camera.transform.y_axis) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(camera.transform.y_axis);
	const quader::QVec3 forward = vec3_is_tiny(camera.transform.z_axis) ? quader::QVec3 { 0.0F, 0.0F, -1.0F } : normalized_axis(vec3_scale(camera.transform.z_axis, -1.0));
	const double width = std::max(1.0, static_cast<double>(camera.viewport_size.x));
	const double height = std::max(1.0, static_cast<double>(camera.viewport_size.y));
	const double aspect = width / height;
	const quader::QVec3 local = point - origin;
	const double view_distance = vec3_dot(local, forward);
	if (camera.projection == CameraProjection::orthographic) {
		const double half_height = std::max(0.0001, camera.orthographic_size * 0.5);
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
	if (camera.projection == CameraProjection::orthographic) {
		return false;
	}
	const quader::QVec3 forward = vec3_is_tiny(camera.transform.z_axis) ? quader::QVec3 { 0.0F, 0.0F, -1.0F } : normalized_axis(vec3_scale(camera.transform.z_axis, -1.0));
	return vec3_dot(point - camera.transform.origin, forward) <= 0.0001F;
}

quader_poly::Ray ray_from_viewport_screen_point(const ViewportCameraState& camera, quader::QVec2 screen)
{
	const quader::QVec3 origin = camera.transform.origin;
	const quader::QVec3 right = vec3_is_tiny(camera.transform.x_axis) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : normalized_axis(camera.transform.x_axis);
	const quader::QVec3 up = vec3_is_tiny(camera.transform.y_axis) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(camera.transform.y_axis);
	const quader::QVec3 forward = vec3_is_tiny(camera.transform.z_axis) ? quader::QVec3 { 0.0F, 0.0F, -1.0F } : normalized_axis(vec3_scale(camera.transform.z_axis, -1.0));
	const double width = std::max(1.0, static_cast<double>(camera.viewport_size.x));
	const double height = std::max(1.0, static_cast<double>(camera.viewport_size.y));
	const double aspect = width / height;
	const double ndc_x = (static_cast<double>(screen.x) / width) * 2.0 - 1.0;
	const double ndc_y = 1.0 - (static_cast<double>(screen.y) / height) * 2.0;
	if (camera.projection == CameraProjection::orthographic) {
		const double half_height = std::max(0.0001, camera.orthographic_size * 0.5);
		return {
			origin +
				vec3_scale(right, ndc_x * half_height * aspect) +
				vec3_scale(up, ndc_y * half_height),
			forward,
		};
	}

	const double fov = std::clamp(camera.fov_degrees, 1.0, 179.0) * std::acos(-1.0) / 180.0;
	const double tangent = std::tan(fov * 0.5);
	quader::QVec3 direction =
		forward +
		vec3_scale(right, ndc_x * tangent * aspect) +
		vec3_scale(up, ndc_y * tangent);
	direction = vec3_is_tiny(direction) ? forward : normalized_axis(direction);
	return { origin, direction };
}

quader::QVec3 cut_grid_plane_normal_for_camera(const ViewportCameraState& camera)
{
	if (camera.projection != CameraProjection::orthographic) {
		return { 0.0F, 1.0F, 0.0F };
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
	const quader_geometry::QLinePlaneHit<double> hit = quader_geometry::intersect_line_plane(
		to_geometry_line(ray),
		plane,
		tool_epsilon);
	quader::QVec3 point;
	if (hit.type == quader_geometry::QPlaneIntersectionType::point) {
		point = to_poly_vec3(quader_geometry::project_point_to_plane(hit.point, plane));
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

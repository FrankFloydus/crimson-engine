////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/interactions/transform_gizmo.h>
#include <modeling/interactions/tool_drag_math.h>
#include <modeling/interactions/tool_transform_math.h>
#include <modeling/runtime/tool_constants.h>
#include <diagnostics/profile.h>
#include <geometry/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace quader_editor_core {

namespace {

quader_geometry::Vec2d tool_geometry_vec2(quader_poly::Vec2 value)
{
	return { value.x, value.y };
}

quader_geometry::Vec3f tool_geometry_vec3(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

quader_poly::Vec3 tool_poly_vec3(quader_geometry::Vec3f value)
{
	return { value.x, value.y, value.z };
}

double vec2_distance_squared(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return quader_geometry::length_squared(tool_geometry_vec2(left) - tool_geometry_vec2(right));
}

bool vec3_is_tiny(quader_poly::Vec3 value, double epsilon = tool_epsilon)
{
	return quader_geometry::length_squared(tool_geometry_vec3(value)) <= static_cast<float>(epsilon * epsilon);
}

quader_poly::Vec3 vec3_scale(quader_poly::Vec3 value, double scalar)
{
	return tool_poly_vec3(tool_geometry_vec3(value) * static_cast<float>(scalar));
}

double vec3_dot(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return quader_geometry::dot(tool_geometry_vec3(left), tool_geometry_vec3(right));
}

quader_poly::Vec3 normalized_axis(quader_poly::Vec3 value)
{
	return tool_poly_vec3(quader_geometry::normalize_or_zero(tool_geometry_vec3(value), 0.000001F));
}

} // namespace

TransformGizmoAxisBasis transform_gizmo_axis_basis(TransformAxis axis, const quader_poly::Transform3& global_transform)
{
	TransformGizmoAxisBasis basis;
	basis.axis = axis;
	basis.local_direction = rotation_axis_vector(axis);
	if (vec3_is_tiny(basis.local_direction)) {
		basis.axis = TransformAxis::none;
		return basis;
	}
	if (axis == TransformAxis::x) {
		basis.ring_world_a = { 0.0F, 1.0F, 0.0F };
		basis.ring_world_b = { 0.0F, 0.0F, 1.0F };
	} else if (axis == TransformAxis::y) {
		basis.ring_world_a = { 1.0F, 0.0F, 0.0F };
		basis.ring_world_b = { 0.0F, 0.0F, 1.0F };
	} else {
		basis.ring_world_a = { 1.0F, 0.0F, 0.0F };
		basis.ring_world_b = { 0.0F, 1.0F, 0.0F };
	}
	basis.world_direction = normalized_axis(transform_vector(global_transform, basis.local_direction));
	basis.ring_world_a = normalized_axis(transform_vector(global_transform, basis.ring_world_a));
	basis.ring_world_b = normalized_axis(transform_vector(global_transform, basis.ring_world_b));
	if (vec3_is_tiny(basis.world_direction)) {
		basis.world_direction = basis.local_direction;
	}
	return basis;
}

double transform_gizmo_axis_visibility_score(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis)
{
	if (vec3_is_tiny(world_axis)) {
		return 0.0;
	}
	const quader_poly::Vec3 axis = normalized_axis(world_axis);
	const quader_poly::Vec3 view_direction = camera_origin - world_center;
	if (vec3_is_tiny(view_direction)) {
		return 1.0;
	}
	return std::clamp(1.0 - std::abs(vec3_dot(axis, normalized_axis(view_direction))), 0.0, 1.0);
}

double transform_gizmo_axis_projection_scale(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis)
{
	if (vec3_is_tiny(world_axis)) {
		return 0.0;
	}
	const quader_poly::Vec3 axis = normalized_axis(world_axis);
	const quader_poly::Vec3 view_direction = camera_origin - world_center;
	if (vec3_is_tiny(view_direction)) {
		return 1.0;
	}
	const double axis_dot_view = std::clamp(std::abs(vec3_dot(axis, normalized_axis(view_direction))), 0.0, 1.0);
	return std::sqrt(std::max(0.0, 1.0 - axis_dot_view * axis_dot_view));
}

bool transform_gizmo_axis_is_visible(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis)
{
	return transform_gizmo_axis_visibility_score(camera_origin, world_center, world_axis) >= transform_gizmo_axis_hide_score;
}

double transform_gizmo_axis_alpha(quader_poly::Vec3 camera_origin, quader_poly::Vec3 world_center, quader_poly::Vec3 world_axis)
{
	const double score = transform_gizmo_axis_visibility_score(camera_origin, world_center, world_axis);
	if (score <= transform_gizmo_axis_hide_score) {
		return 0.0;
	}
	if (score >= transform_gizmo_axis_full_score) {
		return 1.0;
	}
	return (score - transform_gizmo_axis_hide_score) / (transform_gizmo_axis_full_score - transform_gizmo_axis_hide_score);
}

quader_poly::Vec3 transform_gizmo_ring_point(quader_poly::Vec3 world_center, const TransformGizmoAxisBasis& axis, double radius, double angle)
{
	return world_center + vec3_scale(axis.ring_world_a * static_cast<float>(std::cos(angle)) + axis.ring_world_b * static_cast<float>(std::sin(angle)), radius);
}

namespace {

quader_geometry::Vec2d geometry_vec2(quader_poly::Vec2 value)
{
	return { static_cast<double>(value.x), static_cast<double>(value.y) };
}

double clamped_gizmo_scale(double scale)
{
	return std::clamp(scale, 0.5, 2.0);
}

double scaled_gizmo_pick_radius(double base_radius, double scale)
{
	return std::max(8.0, std::max(1.0, base_radius) * clamped_gizmo_scale(scale));
}

bool transform_gizmo_tool_has_frame(ViewportInteractionTool tool)
{
	return tool == ViewportInteractionTool::move ||
		tool == ViewportInteractionTool::rotate ||
		tool == ViewportInteractionTool::scale;
}

double viewport_world_units_per_pixel_impl(const ViewportCameraState& camera, quader_poly::Vec3 anchor)
{
	CameraDragState drag;
	drag.valid = true;
	drag.projection = camera.projection;
	drag.origin = camera.transform.origin;
	drag.right = camera.transform.x_axis;
	drag.up = camera.transform.y_axis;
	drag.forward = vec3_scale(camera.transform.z_axis, -1.0);
	drag.viewport_size = camera.viewport_size;
	drag.orthographic_size = camera.orthographic_size;
	drag.fov_degrees = camera.fov_degrees;
	return drag_world_units_per_pixel(drag, anchor);
}

void add_gizmo_axis(TransformGizmoFrame& frame, const ViewportCameraState& camera, TransformAxis axis, quader_poly::Vec3 center, double axis_length)
{
	const quader_poly::Vec3 direction = axis_vector(axis);
	if (vec3_is_tiny(direction)) {
		return;
	}
	const quader_poly::Vec3 tip = center + vec3_scale(direction, axis_length);
	quader_poly::Vec2 screen_start;
	quader_poly::Vec2 screen_tip;
	if (!project_viewport_point(camera, center, screen_start) || !project_viewport_point(camera, tip, screen_tip)) {
		return;
	}
	if (vec2_distance_squared(screen_start, screen_tip) <= 0.0001) {
		return;
	}
	TransformGizmoAxisPrimitive primitive;
	primitive.axis = axis;
	primitive.world_start = center;
	primitive.world_tip = tip;
	primitive.screen_start = screen_start;
	primitive.screen_tip = screen_tip;
	primitive.alpha = 1.0F;
	frame.axes.push_back(primitive);
}

void add_gizmo_plane(
		TransformGizmoFrame& frame,
		const ViewportCameraState& camera,
		TransformAxis axis,
		quader_poly::Vec3 center,
		quader_poly::Vec3 axis_a,
		quader_poly::Vec3 axis_b,
		double offset,
		double half_size)
{
	const quader_poly::Vec3 a = vec3_is_tiny(axis_a) ? quader_poly::Vec3 { 1.0F, 0.0F, 0.0F } : normalized_axis(axis_a);
	const quader_poly::Vec3 b = vec3_is_tiny(axis_b) ? quader_poly::Vec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(axis_b);
	const quader_poly::Vec3 handle_center = center + vec3_scale(a + b, offset);
	const std::array<quader_poly::Vec3, 4> world {
		handle_center - vec3_scale(a, half_size) - vec3_scale(b, half_size),
		handle_center + vec3_scale(a, half_size) - vec3_scale(b, half_size),
		handle_center + vec3_scale(a, half_size) + vec3_scale(b, half_size),
		handle_center - vec3_scale(a, half_size) + vec3_scale(b, half_size),
	};
	std::array<quader_poly::Vec2, 4> screen {};
	for (std::size_t index = 0; index < world.size(); ++index) {
		if (!project_viewport_point(camera, world[index], screen[index])) {
			return;
		}
	}
	TransformGizmoPlanePrimitive primitive;
	primitive.axis = axis;
	primitive.world0 = world[0];
	primitive.world1 = world[1];
	primitive.world2 = world[2];
	primitive.world3 = world[3];
	primitive.screen0 = screen[0];
	primitive.screen1 = screen[1];
	primitive.screen2 = screen[2];
	primitive.screen3 = screen[3];
	frame.planes.push_back(primitive);
}

double projected_axis_length(const ViewportCameraState& camera, quader_poly::Vec3 center, quader_poly::Vec3 axis, double probe_length)
{
	quader_poly::Vec2 screen_center;
	quader_poly::Vec2 screen_tip;
	if (!project_viewport_point(camera, center, screen_center) ||
			!project_viewport_point(camera, center + vec3_scale(axis, probe_length), screen_tip)) {
		return 0.0;
	}
	return std::sqrt(vec2_distance_squared(screen_center, screen_tip));
}

double screen_sized_world_radius(const ViewportCameraState& camera, quader_poly::Vec3 center, quader_poly::Vec3 ring_a, quader_poly::Vec3 ring_b, double screen_size)
{
	const double camera_distance = std::sqrt(quader_poly::length_squared(camera.transform.origin - center));
	const double probe_length = std::max(1.0, camera_distance * 0.05);
	const double projected_a = projected_axis_length(camera, center, ring_a, probe_length);
	const double projected_b = projected_axis_length(camera, center, ring_b, probe_length);
	const double projected = std::max(projected_a, projected_b);
	if (projected <= 0.0001) {
		return 0.0;
	}
	return probe_length * std::max(24.0, screen_size) / projected;
}

void add_gizmo_ring(TransformGizmoFrame& frame, const ViewportCameraState& camera, TransformAxis axis, quader_poly::Vec3 center, double screen_size, int ring_segment_count)
{
	const TransformGizmoAxisBasis basis = transform_gizmo_axis_basis(axis);
	if (basis.axis == TransformAxis::none) {
		return;
	}
	const double radius = screen_sized_world_radius(camera, center, basis.ring_world_a, basis.ring_world_b, screen_size);
	if (radius <= 0.0001) {
		return;
	}
	const int segment_count = std::max(3, ring_segment_count);
	for (int index = 0; index < segment_count; ++index) {
		const double a0 = transform_gizmo_tau * static_cast<double>(index) / static_cast<double>(segment_count);
		const double a1 = transform_gizmo_tau * static_cast<double>(index + 1) / static_cast<double>(segment_count);
		const quader_poly::Vec3 p0 = transform_gizmo_ring_point(center, basis, radius, a0);
		const quader_poly::Vec3 p1 = transform_gizmo_ring_point(center, basis, radius, a1);
		quader_poly::Vec2 screen0;
		quader_poly::Vec2 screen1;
		if (!project_viewport_point(camera, p0, screen0) || !project_viewport_point(camera, p1, screen1)) {
			continue;
		}
		TransformGizmoRingSegmentPrimitive segment;
		segment.axis = axis;
		segment.screen_start = screen0;
		segment.screen_end = screen1;
		frame.ring_segments.push_back(segment);
	}
}

bool point_in_convex_gizmo_polygon(quader_poly::Vec2 point, const std::array<quader_poly::Vec2, 4>& polygon)
{
	std::array<quader_geometry::Vec2d, 4> geometry_polygon {};
	for (std::size_t index = 0; index < polygon.size(); ++index) {
		geometry_polygon[index] = geometry_vec2(polygon[index]);
	}
	const double signed_area = quader_geometry::polygon_signed_area(
			std::span<const quader_geometry::Vec2d>(geometry_polygon.data(), geometry_polygon.size())) * 2.0;
	if (std::abs(signed_area) <= 0.001) {
		return false;
	}

	return quader_geometry::point_in_or_on_polygon_2d(
		geometry_vec2(point),
		std::span<const quader_geometry::Vec2d>(geometry_polygon.data(), geometry_polygon.size()),
		0.001);
}

quader_poly::Vec2 plane_centroid(const TransformGizmoPlanePrimitive& plane)
{
	return {
		(plane.screen0.x + plane.screen1.x + plane.screen2.x + plane.screen3.x) * 0.25F,
		(plane.screen0.y + plane.screen1.y + plane.screen2.y + plane.screen3.y) * 0.25F,
	};
}

} // namespace

double viewport_world_units_per_pixel(const ViewportCameraState& camera, quader_poly::Vec3 anchor)
{
	return viewport_world_units_per_pixel_impl(camera, anchor);
}

TransformGizmoFrame build_transform_gizmo_frame(const TransformGizmoFrameInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.build_transform_gizmo_frame");
	TransformGizmoFrame frame;
	frame.info.tool = input.tool;
	frame.info.hover_axis = input.hover_axis;
	frame.info.active_axis = input.active_axis;
	frame.info.gizmo_scale = clamped_gizmo_scale(input.gizmo_scale);
	if (!input.has_selection || !input.has_pivot || !transform_gizmo_tool_has_frame(input.tool)) {
		return frame;
	}

	quader_poly::Vec2 screen_pivot;
	if (!project_viewport_point(input.camera, input.pivot, screen_pivot)) {
		return frame;
	}

	const double visual_scale = clamped_gizmo_scale(input.gizmo_scale);
	const double screen_size = std::max(1.0, input.gizmo_size_pixels) * visual_scale;
	frame.info.ok = true;
	frame.info.pivot = input.pivot;
	frame.info.screen_pivot = screen_pivot;

	if (input.tool == ViewportInteractionTool::rotate) {
		add_gizmo_ring(frame, input.camera, TransformAxis::x, input.pivot, screen_size, input.ring_segments);
		add_gizmo_ring(frame, input.camera, TransformAxis::y, input.pivot, screen_size, input.ring_segments);
		add_gizmo_ring(frame, input.camera, TransformAxis::z, input.pivot, screen_size, input.ring_segments);
		return frame;
	}

	const double pixel_world = viewport_world_units_per_pixel(input.camera, input.pivot);
	const double axis_length = pixel_world * screen_size;
	const double plane_offset = pixel_world * (screen_size * 0.36);
	const double plane_half_size = pixel_world * (8.0 * visual_scale);
	add_gizmo_axis(frame, input.camera, TransformAxis::x, input.pivot, axis_length);
	add_gizmo_axis(frame, input.camera, TransformAxis::y, input.pivot, axis_length);
	add_gizmo_axis(frame, input.camera, TransformAxis::z, input.pivot, axis_length);
	add_gizmo_plane(frame, input.camera, TransformAxis::xy, input.pivot, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, plane_offset, plane_half_size);
	add_gizmo_plane(frame, input.camera, TransformAxis::xz, input.pivot, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F }, plane_offset, plane_half_size);
	add_gizmo_plane(frame, input.camera, TransformAxis::yz, input.pivot, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F }, plane_offset, plane_half_size);
	return frame;
}

TransformGizmoPickHit pick_transform_gizmo(const TransformGizmoFrameInput& input, quader_poly::Vec2 screen_position)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.pick_transform_gizmo");
	TransformGizmoPickHit result;
	if (!transform_gizmo_tool_has_frame(input.tool)) {
		return result;
	}
	const TransformGizmoFrame frame = build_transform_gizmo_frame(input);
	if (!frame.info.ok) {
		return result;
	}
	const double pick_radius = scaled_gizmo_pick_radius(input.pick_radius_pixels, frame.info.gizmo_scale);

	if (input.tool == ViewportInteractionTool::move || input.tool == ViewportInteractionTool::scale) {
		for (const TransformGizmoPlanePrimitive& plane : frame.planes) {
			const std::array<quader_poly::Vec2, 4> polygon {
				plane.screen0,
				plane.screen1,
				plane.screen2,
				plane.screen3,
			};
			if (!point_in_convex_gizmo_polygon(screen_position, polygon)) {
				continue;
			}
			result.hit = true;
			result.axis = plane.axis;
			result.local_direction = axis_vector(plane.axis);
			result.world_direction = result.local_direction;
			result.screen_tip = plane_centroid(plane);
			return result;
		}
	}

	if (input.tool == ViewportInteractionTool::rotate) {
		std::vector<TransformGizmoRingPickCandidate> candidates;
		candidates.reserve(frame.ring_segments.size());
		for (const TransformGizmoRingSegmentPrimitive& segment : frame.ring_segments) {
			TransformGizmoRingPickCandidate candidate;
			candidate.axis = segment.axis;
			candidate.local_direction = rotation_axis_vector(segment.axis);
			candidate.world_direction = candidate.local_direction;
			candidate.a = segment.screen_start;
			candidate.b = segment.screen_end;
			candidates.push_back(candidate);
		}
		return pick_rotate_gizmo_axis(screen_position, candidates, pick_radius);
	}

	std::vector<TransformGizmoPickCandidate> candidates;
	candidates.reserve(frame.axes.size());
	for (const TransformGizmoAxisPrimitive& axis : frame.axes) {
		TransformGizmoPickCandidate candidate;
		candidate.axis = axis.axis;
		candidate.local_direction = axis_vector(axis.axis);
		candidate.world_direction = candidate.local_direction;
		candidate.screen_tip = axis.screen_tip;
		candidate.visible = true;
		candidates.push_back(candidate);
	}
	if (input.tool == ViewportInteractionTool::scale) {
		return pick_scale_gizmo_axis(
				screen_position,
				frame.info.screen_pivot,
				candidates,
				pick_radius,
				std::max(1.0, input.gizmo_size_pixels) * clamped_gizmo_scale(frame.info.gizmo_scale));
	}
	return pick_move_gizmo_axis(screen_position, frame.info.screen_pivot, candidates, pick_radius);
}

double distance_to_screen_segment_squared(quader_poly::Vec2 point, quader_poly::Vec2 a, quader_poly::Vec2 b)
{
	const quader_geometry::Vec2d geometry_point = geometry_vec2(point);
	const quader_geometry::Vec2d geometry_a = geometry_vec2(a);
	const quader_geometry::Vec2d geometry_b = geometry_vec2(b);
	const quader_geometry::ClosestSegmentPoint2<double> closest =
			quader_geometry::closest_point_on_segment<double>(geometry_point, geometry_a, geometry_b, std::sqrt(screen_epsilon));
	if (!closest.valid || quader_geometry::length_squared(geometry_b - geometry_a) <= screen_epsilon) {
		return vec2_distance_squared(point, a);
	}
	return vec2_distance_squared(point, { static_cast<float>(closest.point.x), static_cast<float>(closest.point.y) });
}

TransformGizmoPickHit pick_move_gizmo_axis(quader_poly::Vec2 screen_position, quader_poly::Vec2 screen_center, const std::vector<TransformGizmoPickCandidate>& candidates, double pick_radius)
{
	TransformGizmoPickHit hit;
	const double radius = std::max(1.0, pick_radius);
	const double radius_squared = radius * radius;
	double best_distance = 0.0;
	int best_index = -1;
	for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
		if (!candidates[i].visible) {
			continue;
		}
		const double distance = vec2_distance_squared(screen_position, candidates[i].screen_tip);
		if (distance <= radius_squared && (best_index < 0 || distance < best_distance)) {
			best_distance = distance;
			best_index = i;
		}
	}
	for (int i = 0; best_index < 0 && i < static_cast<int>(candidates.size()); ++i) {
		if (!candidates[i].visible) {
			continue;
		}
		const double distance = distance_to_screen_segment_squared(screen_position, screen_center, candidates[i].screen_tip);
		if (distance <= radius_squared && (best_index < 0 || distance < best_distance)) {
			best_distance = distance;
			best_index = i;
		}
	}
	if (best_index < 0) {
		return hit;
	}
	const TransformGizmoPickCandidate& candidate = candidates[best_index];
	hit.hit = true;
	hit.axis = candidate.axis;
	hit.local_direction = candidate.local_direction;
	hit.world_direction = candidate.world_direction;
	hit.screen_tip = candidate.screen_tip;
	hit.distance = std::sqrt(best_distance);
	return hit;
}

TransformGizmoPickHit pick_scale_gizmo_axis(quader_poly::Vec2 screen_position, quader_poly::Vec2 screen_pivot, const std::vector<TransformGizmoPickCandidate>& candidates, double pick_radius, double screen_size)
{
	TransformGizmoPickHit hit;
	const double radius = std::max(1.0, pick_radius);
	const double center_safe_radius = std::max(radius, std::min(std::max(24.0, screen_size) * 0.35, std::max(radius * 1.75, 12.0)));
	const double center_distance_squared = vec2_distance_squared(screen_position, screen_pivot);
	if (center_distance_squared <= center_safe_radius * center_safe_radius) {
		hit.hit = true;
		hit.axis = TransformAxis::all;
		hit.local_direction = { 1.0F, 1.0F, 1.0F };
		hit.world_direction = { 1.0F, 1.0F, 1.0F };
		hit.screen_tip = { static_cast<float>(static_cast<double>(screen_pivot.x) + std::max(24.0, screen_size)), screen_pivot.y };
		hit.distance = std::sqrt(center_distance_squared);
		return hit;
	}
	return pick_move_gizmo_axis(screen_position, screen_pivot, candidates, radius);
}

TransformGizmoPickHit pick_rotate_gizmo_axis(quader_poly::Vec2 screen_position, const std::vector<TransformGizmoRingPickCandidate>& candidates, double pick_radius)
{
	TransformGizmoPickHit hit;
	const double radius = std::max(1.0, pick_radius);
	double best_distance = radius * radius;
	int best_index = -1;
	for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
		const double distance = distance_to_screen_segment_squared(screen_position, candidates[i].a, candidates[i].b);
		if (distance < best_distance) {
			best_distance = distance;
			best_index = i;
		}
	}
	if (best_index < 0) {
		return hit;
	}
	const TransformGizmoRingPickCandidate& candidate = candidates[best_index];
	hit.hit = true;
	hit.axis = candidate.axis;
	hit.local_direction = candidate.local_direction;
	hit.world_direction = candidate.world_direction;
	hit.screen_tip = candidate.a;
	hit.distance = std::sqrt(best_distance);
	return hit;
}

} // namespace quader_editor_core

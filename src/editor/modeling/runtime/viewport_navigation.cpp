////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/viewport_navigation.h>
#include <diagnostics/profile.h>
#include <mesh/geometry/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace quader_editor_core {
namespace {

quader_geometry::Vec3f geometry_vec3(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

quader_poly::Vec3 poly_vec3(quader_geometry::Vec3f value)
{
	return { value.x, value.y, value.z };
}

quader_geometry::Vec2d geometry_vec2(quader_poly::Vec2 value)
{
	return { value.x, value.y };
}

quader_poly::Vec2 poly_vec2(quader_geometry::Vec2d value)
{
	return { static_cast<float>(value.x), static_cast<float>(value.y) };
}

quader_poly::Vec2 vec2_sub(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return { left.x - right.x, left.y - right.y };
}

double vec2_dot(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return quader_geometry::dot(geometry_vec2(left), geometry_vec2(right));
}

bool vec2_is_tiny(quader_poly::Vec2 value)
{
	return quader_geometry::length_squared(geometry_vec2(value)) <= screen_epsilon;
}

quader_poly::Vec2 vec2_normalize_or(quader_poly::Vec2 value, quader_poly::Vec2 fallback)
{
	const quader_geometry::Vec2d geometry_value = geometry_vec2(value);
	const double length_squared = quader_geometry::length_squared(geometry_value);
	if (length_squared <= screen_epsilon) {
		return fallback;
	}
	const double length = std::sqrt(length_squared);
	return poly_vec2(geometry_value / length);
}

double geometry_dot(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return quader_geometry::dot(geometry_vec3(left), geometry_vec3(right));
}

quader_poly::Vec3 geometry_cross(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return poly_vec3(quader_geometry::cross(geometry_vec3(left), geometry_vec3(right)));
}

double geometry_length_squared(quader_poly::Vec3 value)
{
	return quader_geometry::length_squared(geometry_vec3(value));
}

double geometry_length(quader_poly::Vec3 value)
{
	return quader_geometry::length(geometry_vec3(value));
}

quader_poly::Vec3 geometry_scale(quader_poly::Vec3 value, double scalar)
{
	return poly_vec3(geometry_vec3(value) * static_cast<float>(scalar));
}

quader_poly::Vec3 geometry_normalized_axis(quader_poly::Vec3 value)
{
	return poly_vec3(quader_geometry::normalize_or_zero(geometry_vec3(value), static_cast<float>(tool_epsilon)));
}

quader_poly::Vec3 geometry_transform_vector(const quader_poly::Transform3& transform, quader_poly::Vec3 value)
{
	return poly_vec3(
			geometry_vec3(transform.x_axis) * value.x +
			geometry_vec3(transform.y_axis) * value.y +
			geometry_vec3(transform.z_axis) * value.z);
}

bool geometry_is_tiny(quader_poly::Vec3 value, double epsilon = tool_epsilon)
{
	return geometry_length_squared(value) <= epsilon * epsilon;
}

quader_poly::Vec3 rotate_vector_around_axis(quader_poly::Vec3 value, quader_poly::Vec3 axis, double angle_radians)
{
	const quader_poly::Vec3 normal = geometry_normalized_axis(axis);
	if (geometry_is_tiny(normal)) {
		return value;
	}
	const double c = std::cos(angle_radians);
	const double s = std::sin(angle_radians);
	const double one_minus_c = 1.0 - c;
	return geometry_scale(value, c) +
		geometry_scale(geometry_cross(normal, value), s) +
		geometry_scale(normal, geometry_dot(normal, value) * one_minus_c);
}

quader_poly::Transform3 orthonormalized_basis(quader_poly::Transform3 basis)
{
	quader_poly::Vec3 right = geometry_normalized_axis(basis.x_axis);
	if (geometry_is_tiny(right)) {
		right = { 1.0F, 0.0F, 0.0F };
	}
	quader_poly::Vec3 up = basis.y_axis - geometry_scale(right, geometry_dot(basis.y_axis, right));
	up = geometry_normalized_axis(up);
	if (geometry_is_tiny(up)) {
		const quader_poly::Vec3 fallback = std::abs(right.y) > 0.9 ? quader_poly::Vec3 { 0.0F, 0.0F, 1.0F } : quader_poly::Vec3 { 0.0F, 1.0F, 0.0F };
		up = geometry_normalized_axis(fallback - geometry_scale(right, geometry_dot(fallback, right)));
	}
	quader_poly::Vec3 back = geometry_normalized_axis(geometry_cross(right, up));
	if (geometry_is_tiny(back)) {
		back = { 0.0F, 0.0F, 1.0F };
	}
	up = geometry_normalized_axis(geometry_cross(back, right));
	return { right, up, back, basis.origin };
}

quader_poly::Transform3 rotation_basis(quader_poly::Vec3 axis, double angle_radians)
{
	const quader_poly::Vec3 normal = geometry_normalized_axis(axis);
	if (geometry_is_tiny(normal)) {
		return {};
	}
	return {
		rotate_vector_around_axis({ 1.0F, 0.0F, 0.0F }, normal, angle_radians),
		rotate_vector_around_axis({ 0.0F, 1.0F, 0.0F }, normal, angle_radians),
		rotate_vector_around_axis({ 0.0F, 0.0F, 1.0F }, normal, angle_radians),
		{},
	};
}

quader_poly::Transform3 basis_multiply(const quader_poly::Transform3& left, const quader_poly::Transform3& right)
{
	return {
		geometry_transform_vector(left, right.x_axis),
		geometry_transform_vector(left, right.y_axis),
		geometry_transform_vector(left, right.z_axis),
		{},
	};
}

quader_poly::Transform3 rotated_camera_basis(const quader_poly::Transform3& camera_transform, const quader_poly::Transform3& rotation)
{
	return orthonormalized_basis({
			geometry_transform_vector(rotation, camera_transform.x_axis),
			geometry_transform_vector(rotation, camera_transform.y_axis),
			geometry_transform_vector(rotation, camera_transform.z_axis),
			camera_transform.origin,
	});
}

quader_poly::Transform3 clamped_rotation_to_upright(const quader_poly::Transform3& camera_transform, const quader_poly::Transform3& rotation)
{
	const quader_poly::Vec3 world_up { 0.0F, 1.0F, 0.0F };
	const quader_poly::Vec3 new_up = geometry_normalized_axis(geometry_transform_vector(rotation, camera_transform.y_axis));
	if (geometry_dot(new_up, world_up) >= 0.0) {
		return orthonormalized_basis(rotation);
	}

	const quader_poly::Vec3 new_right = geometry_normalized_axis(geometry_transform_vector(rotation, camera_transform.x_axis));
	const quader_poly::Vec3 new_forward = geometry_normalized_axis(geometry_transform_vector(rotation, geometry_scale(camera_transform.z_axis, -1.0)));
	quader_poly::Vec3 clamped_up = new_up - geometry_scale(world_up, geometry_dot(new_up, world_up));
	clamped_up = geometry_is_tiny(clamped_up) ? quader_poly::Vec3 { 1.0F, 0.0F, 0.0F } : geometry_normalized_axis(clamped_up);

	const double cos_angle = std::clamp(geometry_dot(clamped_up, new_up), -1.0, 1.0);
	const double correction_angle = std::acos(cos_angle);
	const double correction_sign = geometry_dot(new_forward, world_up) > 0.0 ? -1.0 : 1.0;
	const quader_poly::Transform3 correction = rotation_basis(new_right, correction_angle * correction_sign);
	return orthonormalized_basis(basis_multiply(correction, rotation));
}

quader_poly::Transform3 orthogonal_camera_basis_for_offset_axis(quader_poly::Vec3 axis)
{
	const quader_poly::Vec3 world_up { 0.0F, 1.0F, 0.0F };
	const quader_poly::Vec3 back = geometry_is_tiny(axis) ? quader_poly::Vec3 { 0.0F, 0.0F, 1.0F } : geometry_normalized_axis(axis);
	quader_poly::Vec3 right;
	if (std::abs(geometry_dot(back, world_up)) > 0.999) {
		right = { 1.0F, 0.0F, 0.0F };
	} else {
		right = geometry_cross(world_up, back);
	}
	right = geometry_is_tiny(right) ? quader_poly::Vec3 { 1.0F, 0.0F, 0.0F } : geometry_normalized_axis(right);
	const quader_poly::Vec3 up = geometry_normalized_axis(geometry_cross(back, right));
	return orthonormalized_basis({ right, geometry_is_tiny(up) ? world_up : up, back, {} });
}

double basis_alignment_score(const quader_poly::Transform3& left, const quader_poly::Transform3& right)
{
	const quader_poly::Transform3 a = orthonormalized_basis(left);
	const quader_poly::Transform3 b = orthonormalized_basis(right);
	return geometry_dot(a.x_axis, b.x_axis) + geometry_dot(a.y_axis, b.y_axis) + geometry_dot(a.z_axis, b.z_axis);
}

quader_poly::Transform3 closest_orthogonal_camera_basis_for_offset_axis(quader_poly::Vec3 axis, const quader_poly::Transform3& reference_basis)
{
	const quader_poly::Vec3 back = geometry_is_tiny(axis) ? quader_poly::Vec3 { 0.0F, 0.0F, 1.0F } : geometry_normalized_axis(axis);
	const quader_poly::Transform3 base = orthogonal_camera_basis_for_offset_axis(back);
	static constexpr std::array<double, 4> rolls {
		0.0,
		1.5707963267948966,
		3.1415926535897932,
		4.7123889803846899,
	};

	quader_poly::Transform3 best_basis = base;
	double best_score = basis_alignment_score(base, reference_basis);
	for (const double roll : rolls) {
		const quader_poly::Transform3 roll_rotation = rotation_basis(back, roll);
		const quader_poly::Transform3 candidate = rotated_camera_basis(base, roll_rotation);
		const double score = basis_alignment_score(candidate, reference_basis);
		if (score > best_score) {
			best_score = score;
			best_basis = candidate;
		}
	}
	return orthonormalized_basis(best_basis);
}

ViewportNavigationResult navigation_result(const quader_poly::Transform3& transform, quader_poly::Vec3 pivot, double camera_size = -1.0)
{
	ViewportNavigationResult result;
	result.ok = true;
	result.camera_transform = transform;
	result.pivot = pivot;
	if (camera_size > 0.0) {
		result.has_camera_size = true;
		result.camera_size = camera_size;
	}
	return result;
}

double capped_orthographic_zoom_size(double current_size, double factor, double steps)
{
	const double safe_current_size = std::max(navigation_min_orthographic_size, current_size);
	const double requested_size = safe_current_size / factor;
	if (steps < 0.0) {
		if (safe_current_size >= navigation_max_orthographic_zoom_out_size) {
			return safe_current_size;
		}
		return std::clamp(requested_size, navigation_min_orthographic_size, navigation_max_orthographic_zoom_out_size);
	}
	return std::max(navigation_min_orthographic_size, requested_size);
}

bool orthogonal_snap_transform(const quader_poly::Transform3& unsnapped_transform, quader_poly::Vec3 pivot, quader_poly::Transform3& snapped_transform)
{
	const quader_poly::Vec3 offset = unsnapped_transform.origin - pivot;
	const double distance = geometry_length(offset);
	if (distance <= navigation_min_camera_distance) {
		return false;
	}

	const quader_poly::Vec3 direction = offset / static_cast<float>(distance);
	const quader_poly::Vec3 axes[6] = {
		{ 0.0F, 0.0F, 1.0F },
		{ 0.0F, 0.0F, -1.0F },
		{ 1.0F, 0.0F, 0.0F },
		{ -1.0F, 0.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
		{ 0.0F, -1.0F, 0.0F },
	};

	double best_dot = -1.0;
	quader_poly::Vec3 best_axis = axes[0];
	for (const quader_poly::Vec3& axis : axes) {
		const double axis_dot = geometry_dot(direction, axis);
		if (axis_dot > best_dot) {
			best_dot = axis_dot;
			best_axis = axis;
		}
	}
	if (best_dot < navigation_orthogonal_snap_dot) {
		return false;
	}

	snapped_transform = unsnapped_transform;
	snapped_transform.origin = pivot + geometry_scale(best_axis, distance);
	const quader_poly::Transform3 snapped_basis = closest_orthogonal_camera_basis_for_offset_axis(best_axis, unsnapped_transform);
	snapped_transform.x_axis = snapped_basis.x_axis;
	snapped_transform.y_axis = snapped_basis.y_axis;
	snapped_transform.z_axis = snapped_basis.z_axis;
	return true;
}

/**
 * Represents an Axis Target value used by the modeling runtime domain.
 */
struct AxisTarget {
	int axis_index = 2;
	int sign = 1;
};

AxisTarget axis_target_from_vector(quader_poly::Vec3 value)
{
	value = geometry_is_tiny(value) ? quader_poly::Vec3 { 0.0F, 0.0F, 1.0F } : geometry_normalized_axis(value);
	const quader_geometry::Axis axis = quader_geometry::dominant_axis(geometry_vec3(value));
	int axis_index = 2;
	double component = value.z;
	if (axis == quader_geometry::Axis::x) {
		axis_index = 0;
		component = value.x;
	} else if (axis == quader_geometry::Axis::y) {
		axis_index = 1;
		component = value.y;
	}
	return { axis_index, component < 0.0 ? -1 : 1 };
}

AxisTarget current_view_axis_target(const quader_poly::Transform3& camera_transform, quader_poly::Vec3 pivot)
{
	const quader_poly::Vec3 offset = camera_transform.origin - pivot;
	if (geometry_length_squared(offset) > navigation_min_camera_distance * navigation_min_camera_distance) {
		return axis_target_from_vector(offset);
	}
	return axis_target_from_vector(orthonormalized_basis(camera_transform).z_axis);
}

bool axis_target_equals(const AxisTarget& left, const AxisTarget& right)
{
	return left.axis_index == right.axis_index && left.sign == right.sign;
}

int axis_target_ring_index(const AxisTarget& target, const std::array<AxisTarget, 4>& ring)
{
	for (int index = 0; index < static_cast<int>(ring.size()); ++index) {
		if (axis_target_equals(target, ring[index])) {
			return index;
		}
	}
	return -1;
}

AxisTarget stepped_axis_target(const AxisTarget& current, ViewAxisStepDirection direction)
{
	static constexpr std::array<AxisTarget, 4> horizontal_ring {
		AxisTarget { 2, 1 },
		AxisTarget { 0, 1 },
		AxisTarget { 2, -1 },
		AxisTarget { 0, -1 },
	};
	static constexpr std::array<AxisTarget, 4> vertical_ring {
		AxisTarget { 2, 1 },
		AxisTarget { 1, 1 },
		AxisTarget { 2, -1 },
		AxisTarget { 1, -1 },
	};

	if (direction == ViewAxisStepDirection::left || direction == ViewAxisStepDirection::right) {
		const int step = direction == ViewAxisStepDirection::right ? 1 : -1;
		const int ring_index = axis_target_ring_index(current, horizontal_ring);
		if (ring_index >= 0) {
			return horizontal_ring[(ring_index + step + 4) % 4];
		}
		return { 0, step };
	}
	const int step = direction == ViewAxisStepDirection::up ? 1 : -1;
	const int ring_index = axis_target_ring_index(current, vertical_ring);
	if (ring_index >= 0) {
		return vertical_ring[(ring_index + step + 4) % 4];
	}
	return { 1, step };
}

ViewAxis view_axis_from_index(int index)
{
	if (index == 0) {
		return ViewAxis::x;
	}
	if (index == 1) {
		return ViewAxis::y;
	}
	return ViewAxis::z;
}

TransformAxis transform_axis_from_index(int index)
{
	if (index == 0) {
		return TransformAxis::x;
	}
	if (index == 1) {
		return TransformAxis::y;
	}
	if (index == 2) {
		return TransformAxis::z;
	}
	return TransformAxis::none;
}


quader_poly::Vec2 view_cube_center(quader_poly::Vec2 viewport_size, quader_poly::Vec2 margin)
{
	return {
		static_cast<float>(std::max(0.0, static_cast<double>(margin.x))),
		static_cast<float>(std::max(0.0, static_cast<double>(viewport_size.y) - static_cast<double>(margin.y))),
	};
}

quader_poly::Vec2 project_view_cube_point(const quader_poly::Transform3& camera_transform, quader_poly::Vec3 point, quader_poly::Vec2 center, double scale)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);
	return {
		static_cast<float>(static_cast<double>(center.x) + geometry_dot(camera_basis.x_axis, point) * scale),
		static_cast<float>(static_cast<double>(center.y) - geometry_dot(camera_basis.y_axis, point) * scale),
	};
}

double view_cube_depth(const quader_poly::Transform3& camera_transform, quader_poly::Vec3 point)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);
	return geometry_dot(camera_basis.z_axis, point);
}

std::vector<quader_poly::Vec2> make_view_cube_face_polygon(
		const quader_poly::Transform3& camera_transform,
		quader_poly::Vec2 center,
		double scale,
		const std::array<quader_poly::Vec3, 4>& vertices)
{
	std::vector<quader_poly::Vec2> polygon;
	polygon.reserve(vertices.size());
	for (quader_poly::Vec3 vertex : vertices) {
		polygon.push_back(project_view_cube_point(camera_transform, vertex, center, scale));
	}
	return polygon;
}

quader_poly::Vec2 polygon_center(const std::vector<quader_poly::Vec2>& polygon)
{
	if (polygon.empty()) {
		return {};
	}
	double x = 0.0;
	double y = 0.0;
	for (quader_poly::Vec2 point : polygon) {
		x += point.x;
		y += point.y;
	}
	const double size = static_cast<double>(polygon.size());
	return { static_cast<float>(x / size), static_cast<float>(y / size) };
}

/**
 * Represents a View Cube Face Definition value used by the modeling runtime domain.
 */
struct ViewCubeFaceDefinition {
	int axis_index;
	int sign;
	quader_poly::Vec3 normal;
	std::array<quader_poly::Vec3, 4> vertices;
};

const std::array<ViewCubeFaceDefinition, 6>& view_cube_face_definitions()
{
	static const std::array<ViewCubeFaceDefinition, 6> definitions = { {
		{ 0, 1, { 1.0F, 0.0F, 0.0F }, { quader_poly::Vec3 { 1.0F, -1.0F, -1.0F }, quader_poly::Vec3 { 1.0F, 1.0F, -1.0F }, quader_poly::Vec3 { 1.0F, 1.0F, 1.0F }, quader_poly::Vec3 { 1.0F, -1.0F, 1.0F } } },
		{ 0, -1, { -1.0F, 0.0F, 0.0F }, { quader_poly::Vec3 { -1.0F, -1.0F, 1.0F }, quader_poly::Vec3 { -1.0F, 1.0F, 1.0F }, quader_poly::Vec3 { -1.0F, 1.0F, -1.0F }, quader_poly::Vec3 { -1.0F, -1.0F, -1.0F } } },
		{ 1, 1, { 0.0F, 1.0F, 0.0F }, { quader_poly::Vec3 { -1.0F, 1.0F, -1.0F }, quader_poly::Vec3 { -1.0F, 1.0F, 1.0F }, quader_poly::Vec3 { 1.0F, 1.0F, 1.0F }, quader_poly::Vec3 { 1.0F, 1.0F, -1.0F } } },
		{ 1, -1, { 0.0F, -1.0F, 0.0F }, { quader_poly::Vec3 { -1.0F, -1.0F, 1.0F }, quader_poly::Vec3 { -1.0F, -1.0F, -1.0F }, quader_poly::Vec3 { 1.0F, -1.0F, -1.0F }, quader_poly::Vec3 { 1.0F, -1.0F, 1.0F } } },
		{ 2, 1, { 0.0F, 0.0F, 1.0F }, { quader_poly::Vec3 { -1.0F, -1.0F, 1.0F }, quader_poly::Vec3 { 1.0F, -1.0F, 1.0F }, quader_poly::Vec3 { 1.0F, 1.0F, 1.0F }, quader_poly::Vec3 { -1.0F, 1.0F, 1.0F } } },
		{ 2, -1, { 0.0F, 0.0F, -1.0F }, { quader_poly::Vec3 { 1.0F, -1.0F, -1.0F }, quader_poly::Vec3 { -1.0F, -1.0F, -1.0F }, quader_poly::Vec3 { -1.0F, 1.0F, -1.0F }, quader_poly::Vec3 { 1.0F, 1.0F, -1.0F } } },
	} };
	return definitions;
}

bool view_cube_same_corner(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool view_cube_same_edge(quader_poly::Vec3 left_start, quader_poly::Vec3 left_end, quader_poly::Vec3 right_start, quader_poly::Vec3 right_end)
{
	return (view_cube_same_corner(left_start, right_start) && view_cube_same_corner(left_end, right_end)) ||
		(view_cube_same_corner(left_start, right_end) && view_cube_same_corner(left_end, right_start));
}

ViewAxis view_cube_edge_axis(quader_poly::Vec3 start, quader_poly::Vec3 end)
{
	if (std::abs(start.x - end.x) > 0.5F) {
		return ViewAxis::x;
	}
	if (std::abs(start.y - end.y) > 0.5F) {
		return ViewAxis::y;
	}
	return ViewAxis::z;
}

bool point_in_polygon(const std::vector<quader_poly::Vec2>& polygon, quader_poly::Vec2 point)
{
	std::vector<quader_geometry::Vec2d> geometry_polygon;
	geometry_polygon.reserve(polygon.size());
	for (quader_poly::Vec2 vertex : polygon) {
		geometry_polygon.push_back(geometry_vec2(vertex));
	}
	return quader_geometry::point_in_or_on_polygon_2d(
		geometry_vec2(point),
		std::span<const quader_geometry::Vec2d>(geometry_polygon),
		tool_epsilon);
}

std::vector<ViewCubeFacePrimitive> build_view_cube_faces_for_state(const quader_poly::Transform3& camera_transform, quader_poly::Vec2 center, double scale)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);

	std::vector<ViewCubeFacePrimitive> faces;
	for (const ViewCubeFaceDefinition& definition : view_cube_face_definitions()) {
		const double normal_depth = geometry_dot(camera_basis.z_axis, definition.normal);
		if (normal_depth <= view_cube_face_visibility_epsilon) {
			continue;
		}
		ViewCubeFacePrimitive face;
		face.axis = view_axis_from_index(definition.axis_index);
		face.sign = definition.sign;
		face.depth = normal_depth;
		face.polygon = make_view_cube_face_polygon(camera_transform, center, scale, definition.vertices);
		face.screen_position = polygon_center(face.polygon);
		faces.push_back(face);
	}
	std::sort(faces.begin(), faces.end(), [](const ViewCubeFacePrimitive& left, const ViewCubeFacePrimitive& right) {
		return left.depth < right.depth;
	});
	return faces;
}

std::vector<ViewCubeEdgePrimitive> build_view_cube_edges_for_state(const quader_poly::Transform3& camera_transform, quader_poly::Vec2 center, double scale)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);
	/**
	 * Stores the Edge Key data contract used by the modeling runtime domain.
	 */
	struct EdgeKey {
		quader_poly::Vec3 start;
		quader_poly::Vec3 end;
	};

	std::vector<EdgeKey> emitted_edges;
	std::vector<ViewCubeEdgePrimitive> edges;
	for (const ViewCubeFaceDefinition& definition : view_cube_face_definitions()) {
		const double normal_depth = geometry_dot(camera_basis.z_axis, definition.normal);
		if (normal_depth <= view_cube_face_visibility_epsilon) {
			continue;
		}

		for (std::size_t index = 0; index < definition.vertices.size(); ++index) {
			const quader_poly::Vec3 start = definition.vertices[index];
			const quader_poly::Vec3 end = definition.vertices[(index + 1u) % definition.vertices.size()];
			const bool already_emitted = std::any_of(
				emitted_edges.begin(),
				emitted_edges.end(),
				[&](const EdgeKey& edge) {
					return view_cube_same_edge(start, end, edge.start, edge.end);
				});
			if (already_emitted) {
				continue;
			}

			emitted_edges.push_back({ start, end });
			ViewCubeEdgePrimitive edge;
			edge.axis = view_cube_edge_axis(start, end);
			edge.depth = (view_cube_depth(camera_transform, start) + view_cube_depth(camera_transform, end)) * 0.5;
			edge.local_start = start;
			edge.local_end = end;
			edge.screen_start = project_view_cube_point(camera_transform, start, center, scale);
			edge.screen_end = project_view_cube_point(camera_transform, end, center, scale);
			edges.push_back(edge);
		}
	}

	std::sort(edges.begin(), edges.end(), [](const ViewCubeEdgePrimitive& left, const ViewCubeEdgePrimitive& right) {
		return left.depth < right.depth;
	});
	return edges;
}

std::vector<ViewCubeEdgePrimitive> build_view_cube_axis_edges_for_state(const quader_poly::Transform3& camera_transform, quader_poly::Vec2 center, double scale)
{
	const quader_poly::Vec3 anchor { 1.0F, -1.0F, 1.0F };
	const struct AxisEdgeDefinition {
		ViewAxis axis;
		quader_poly::Vec3 end;
	} axis_edges[3] = {
		{ ViewAxis::x, { -1.0F, -1.0F, 1.0F } },
		{ ViewAxis::y, { 1.0F, 1.0F, 1.0F } },
		{ ViewAxis::z, { 1.0F, -1.0F, -1.0F } },
	};

	std::vector<ViewCubeEdgePrimitive> edges;
	edges.reserve(3u);
	for (const AxisEdgeDefinition& definition : axis_edges) {
		ViewCubeEdgePrimitive edge;
		edge.axis = definition.axis;
		edge.depth = (view_cube_depth(camera_transform, anchor) + view_cube_depth(camera_transform, definition.end)) * 0.5;
		edge.local_start = anchor;
		edge.local_end = definition.end;
		edge.screen_start = project_view_cube_point(camera_transform, anchor, center, scale);
		edge.screen_end = project_view_cube_point(camera_transform, definition.end, center, scale);
		edges.push_back(edge);
	}
	return edges;
}

std::vector<ViewCubeAxisMarkerPrimitive> build_view_cube_markers_for_state(const quader_poly::Transform3& camera_transform, quader_poly::Vec2 center, double radius)
{
	std::vector<ViewCubeAxisMarkerPrimitive> markers;
	const quader_poly::Vec3 axes[3] = {
		{ 1.0F, 0.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
		{ 0.0F, 0.0F, 1.0F },
	};
	for (int axis_index = 0; axis_index < 3; ++axis_index) {
		quader_poly::Vec2 projected = project_view_cube_point(camera_transform, axes[axis_index], {}, 1.0);
		const double depth = view_cube_depth(camera_transform, axes[axis_index]);
		if (vec2_is_tiny(projected)) {
			projected = depth >= 0.0 ? quader_poly::Vec2 { 0.0F, -1.0F } : quader_poly::Vec2 { 0.0F, 1.0F };
		}
		const quader_poly::Vec2 normalized = vec2_normalize_or(projected, { 0.0F, -1.0F });
		ViewCubeAxisMarkerPrimitive marker;
		marker.axis = view_axis_from_index(axis_index);
		marker.sign = 1;
		marker.depth = depth;
		marker.screen_position = {
			static_cast<float>(static_cast<double>(center.x) + static_cast<double>(normalized.x) * radius),
			static_cast<float>(static_cast<double>(center.y) + static_cast<double>(normalized.y) * radius),
		};
		markers.push_back(marker);
	}
	std::sort(markers.begin(), markers.end(), [](const ViewCubeAxisMarkerPrimitive& left, const ViewCubeAxisMarkerPrimitive& right) {
		return left.depth < right.depth;
	});
	return markers;
}

} // namespace

quader_poly::Vec3 view_axis_vector(ViewAxis axis, int sign)
{
	const float value = sign < 0 ? -1.0F : 1.0F;
	if (axis == ViewAxis::x) {
		return { value, 0.0F, 0.0F };
	}
	if (axis == ViewAxis::y) {
		return { 0.0F, value, 0.0F };
	}
	return { 0.0F, 0.0F, value };
}

quader_poly::Vec3 viewport_camera_forward(const quader_poly::Transform3& camera_transform)
{
	const quader_poly::Vec3 forward = geometry_scale(camera_transform.z_axis, -1.0);
	return geometry_is_tiny(forward) ? quader_poly::Vec3 { 0.0F, 0.0F, -1.0F } : geometry_normalized_axis(forward);
}

quader_poly::Vec3 viewport_camera_right(const quader_poly::Transform3& camera_transform)
{
	return geometry_is_tiny(camera_transform.x_axis) ? quader_poly::Vec3 { 1.0F, 0.0F, 0.0F } : geometry_normalized_axis(camera_transform.x_axis);
}

quader_poly::Vec3 viewport_camera_up(const quader_poly::Transform3& camera_transform)
{
	return geometry_is_tiny(camera_transform.y_axis) ? quader_poly::Vec3 { 0.0F, 1.0F, 0.0F } : geometry_normalized_axis(camera_transform.y_axis);
}

double viewport_distance_to_pivot(const quader_poly::Transform3& camera_transform, quader_poly::Vec3 pivot)
{
	const quader_poly::Vec3 to_pivot = pivot - camera_transform.origin;
	const double forward_distance = geometry_dot(to_pivot, viewport_camera_forward(camera_transform));
	if (forward_distance > navigation_min_camera_distance) {
		return forward_distance;
	}
	return std::max(navigation_min_camera_distance, geometry_length(to_pivot));
}

double viewport_aspect_for_size(quader_poly::Vec2 viewport_size)
{
	const double width = std::max(1.0, static_cast<double>(viewport_size.x));
	const double height = std::max(1.0, static_cast<double>(viewport_size.y));
	return width / height;
}

double viewport_vertical_fov_radians(double fov_degrees)
{
	const double clamped_fov = std::clamp(fov_degrees, 1.0, 179.0);
	return clamped_fov * std::acos(-1.0) / 180.0;
}

double viewport_orthographic_size_for_perspective(const quader_poly::Transform3& camera_transform, quader_poly::Vec3 pivot, double fov_degrees)
{
	const double distance = std::max(0.1, geometry_length(camera_transform.origin - pivot));
	return std::max(navigation_min_orthographic_size, 2.0 * distance * std::tan(viewport_vertical_fov_radians(fov_degrees) * 0.5));
}

double viewport_orbit_sensitivity(double mouse_sensitivity)
{
	return navigation_orbit_radians_per_pixel * std::max(0.1, mouse_sensitivity);
}

double viewport_fly_look_sensitivity(double mouse_sensitivity)
{
	return navigation_fly_look_radians_per_pixel * std::max(0.1, mouse_sensitivity);
}

double viewport_fly_move_distance(double delta_seconds, bool fast, bool slow)
{
	double speed = navigation_fly_base_speed;
	if (fast) {
		speed *= navigation_fly_fast_multiplier;
	}
	if (slow) {
		speed *= navigation_fly_slow_multiplier;
	}
	const double clamped_delta = std::clamp(delta_seconds, 0.0, navigation_fly_max_delta_seconds);
	return speed * clamped_delta;
}

void reset_viewport_navigation(ViewportNavigationState& state)
{
	state = ViewportNavigationState();
}

void begin_viewport_navigation(ViewportNavigationState& state, bool pan)
{
	state.mode = pan ? ViewportNavigationMode::pan : ViewportNavigationMode::orbit;
	state.fly_camera_id = 0;
	state.fly_pane_id.clear();
	clear_viewport_orbit_snap_source(state);
	ensure_viewport_navigation_pivot(state);
}

void begin_viewport_fly_navigation(ViewportNavigationState& state, std::uint64_t camera_id, std::string_view pane_id)
{
	state.mode = ViewportNavigationMode::fly;
	state.fly_camera_id = camera_id;
	state.fly_pane_id = std::string(pane_id);
	clear_viewport_orbit_snap_source(state);
}

void end_viewport_navigation(ViewportNavigationState& state)
{
	state.mode = ViewportNavigationMode::none;
	state.fly_camera_id = 0;
	state.fly_pane_id.clear();
	clear_viewport_orbit_snap_source(state);
}

void ensure_viewport_navigation_pivot(ViewportNavigationState& state)
{
	if (!state.has_pivot) {
		state.pivot = {};
		state.has_pivot = true;
	}
}

void clear_viewport_orbit_snap_source(ViewportNavigationState& state)
{
	state.orbit_snap_source_transform = {};
	state.has_orbit_snap_source = false;
}

bool viewport_navigation_is_pan(const ViewportNavigationState& state)
{
	return state.mode == ViewportNavigationMode::pan;
}

bool viewport_navigation_is_orbit(const ViewportNavigationState& state)
{
	return state.mode == ViewportNavigationMode::orbit;
}

bool viewport_navigation_is_fly(const ViewportNavigationState& state)
{
	return state.mode == ViewportNavigationMode::fly;
}

bool viewport_navigation_is_fly_camera(const ViewportNavigationState& state, std::uint64_t camera_id)
{
	return state.mode == ViewportNavigationMode::fly && state.fly_camera_id != 0 && state.fly_camera_id == camera_id;
}

bool viewport_navigation_is_active(const ViewportNavigationState& state, bool view_cube_dragging)
{
	return state.mode != ViewportNavigationMode::none || view_cube_dragging;
}

ViewportNavigationResult viewport_orbit(const ViewportOrbitInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_orbit");
	const double sensitivity = std::max(0.00001, input.sensitivity);
	const double yaw = -static_cast<double>(input.screen_delta.x) * sensitivity;
	const double pitch = -static_cast<double>(input.screen_delta.y) * sensitivity;

	const quader_poly::Transform3 yaw_rotation = rotation_basis({ 0.0F, 1.0F, 0.0F }, yaw);
	const quader_poly::Vec3 right = geometry_normalized_axis(geometry_transform_vector(yaw_rotation, viewport_camera_right(input.camera_transform)));
	const quader_poly::Transform3 pitch_rotation = rotation_basis(geometry_is_tiny(right) ? quader_poly::Vec3 { 1.0F, 0.0F, 0.0F } : right, pitch);
	const quader_poly::Transform3 rotation = clamped_rotation_to_upright(input.camera_transform, basis_multiply(pitch_rotation, yaw_rotation));

	quader_poly::Transform3 unsnapped_result = input.camera_transform;
	const quader_poly::Vec3 rotated_offset = geometry_transform_vector(rotation, input.camera_transform.origin - input.pivot);
	unsnapped_result.origin = input.pivot + rotated_offset;
	const quader_poly::Transform3 rotated_basis = rotated_camera_basis(input.camera_transform, rotation);
	unsnapped_result.x_axis = rotated_basis.x_axis;
	unsnapped_result.y_axis = rotated_basis.y_axis;
	unsnapped_result.z_axis = rotated_basis.z_axis;

	quader_poly::Transform3 visible_result = unsnapped_result;
	const bool snapped = input.snap_to_orthogonal && orthogonal_snap_transform(unsnapped_result, input.pivot, visible_result);
	ViewportNavigationResult result = navigation_result(visible_result, input.pivot);
	result.orthogonal_snap = snapped;
	result.unsnapped_camera_transform = unsnapped_result;
	return result;
}

ViewportNavigationResult viewport_pan(const ViewportPanInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_pan");
	const double viewport_height = std::max(1.0, static_cast<double>(input.camera.viewport_size.y));
	double world_per_pixel = 0.01;
	if (input.camera.projection == CameraProjection::orthographic) {
		world_per_pixel = std::max(0.0001, input.camera.orthographic_size) / viewport_height;
	} else {
		const double distance = std::max(navigation_min_camera_distance, viewport_distance_to_pivot(input.camera.transform, input.pivot));
		const double vertical_span = 2.0 * distance * std::tan(viewport_vertical_fov_radians(input.camera.fov_degrees) * 0.5);
		world_per_pixel = std::max(0.0001, vertical_span) / viewport_height;
	}

	const quader_poly::Vec3 move =
			geometry_scale(viewport_camera_right(input.camera.transform), -static_cast<double>(input.screen_delta.x) * world_per_pixel) +
			geometry_scale(viewport_camera_up(input.camera.transform), static_cast<double>(input.screen_delta.y) * world_per_pixel);
	quader_poly::Transform3 result_transform = input.camera.transform;
	result_transform.origin += move;
	return navigation_result(result_transform, input.pivot + move);
}

ViewportNavigationResult viewport_zoom(const ViewportZoomInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_zoom");
	if (std::abs(input.steps) <= 0.000001) {
		return navigation_result(input.camera.transform, input.pivot);
	}

	const double factor = std::pow(navigation_zoom_step_factor, input.steps);
	if (input.camera.projection == CameraProjection::orthographic) {
		const double new_size = capped_orthographic_zoom_size(input.camera.orthographic_size, factor, input.steps);
		return navigation_result(input.camera.transform, input.pivot, new_size);
	}

	const quader_poly::Vec3 forward = viewport_camera_forward(input.camera.transform);
	const double distance = std::max(navigation_min_camera_distance, viewport_distance_to_pivot(input.camera.transform, input.pivot));
	const double new_distance = std::max(navigation_min_camera_distance, distance / factor);
	quader_poly::Transform3 result_transform = input.camera.transform;
	result_transform.origin += geometry_scale(forward, distance - new_distance);
	return navigation_result(result_transform, input.pivot);
}

ViewportNavigationResult viewport_zoom_to_cursor(const ViewportZoomToCursorInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_zoom_to_cursor");
	if (std::abs(input.steps) <= 0.000001) {
		return navigation_result(input.camera.transform, input.pivot);
	}

	const double factor = std::pow(navigation_zoom_step_factor, input.steps);
	const quader_poly::Vec3 ray_direction = geometry_is_tiny(input.ray_direction) ? viewport_camera_forward(input.camera.transform) : geometry_normalized_axis(input.ray_direction);
	const double fallback_distance = std::max(navigation_min_camera_distance, viewport_distance_to_pivot(input.camera.transform, input.pivot));
	quader_poly::Vec3 cursor_target = input.has_cursor_target ? input.cursor_target : input.ray_origin + geometry_scale(ray_direction, fallback_distance);

	if (input.camera.projection == CameraProjection::orthographic) {
		const double current_size = std::max(navigation_min_orthographic_size, input.camera.orthographic_size);
		const double new_size = capped_orthographic_zoom_size(current_size, factor, input.steps);
		quader_poly::Vec3 cursor_offset = input.ray_origin - input.camera.transform.origin;
		cursor_offset = cursor_offset - geometry_scale(ray_direction, geometry_dot(cursor_offset, ray_direction));
		quader_poly::Transform3 result_transform = input.camera.transform;
		result_transform.origin += geometry_scale(cursor_offset, 1.0 - (new_size / current_size));
		return navigation_result(result_transform, cursor_target, new_size);
	}

	double target_distance = geometry_dot(cursor_target - input.ray_origin, ray_direction);
	if (target_distance <= navigation_min_camera_distance) {
		target_distance = fallback_distance;
		cursor_target = input.ray_origin + geometry_scale(ray_direction, target_distance);
	}
	const double new_distance = std::max(navigation_min_camera_distance, target_distance / factor);
	quader_poly::Transform3 result_transform = input.camera.transform;
	result_transform.origin += geometry_scale(ray_direction, target_distance - new_distance);
	return navigation_result(result_transform, cursor_target);
}

ViewportNavigationResult viewport_fly_look(const ViewportFlyLookInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_fly_look");
	const double sensitivity = std::max(0.00001, input.sensitivity);
	const double yaw = -static_cast<double>(input.screen_delta.x) * sensitivity;
	const double pitch = -static_cast<double>(input.screen_delta.y) * sensitivity;
	const quader_poly::Transform3 yaw_rotation = rotation_basis({ 0.0F, 1.0F, 0.0F }, yaw);
	const quader_poly::Vec3 right = geometry_normalized_axis(geometry_transform_vector(yaw_rotation, viewport_camera_right(input.camera_transform)));
	const quader_poly::Transform3 pitch_rotation = rotation_basis(geometry_is_tiny(right) ? quader_poly::Vec3 { 1.0F, 0.0F, 0.0F } : right, pitch);
	const quader_poly::Transform3 rotation = clamped_rotation_to_upright(input.camera_transform, basis_multiply(pitch_rotation, yaw_rotation));

	quader_poly::Transform3 result_transform = input.camera_transform;
	const quader_poly::Transform3 basis = rotated_camera_basis(input.camera_transform, rotation);
	result_transform.x_axis = basis.x_axis;
	result_transform.y_axis = basis.y_axis;
	result_transform.z_axis = basis.z_axis;
	return navigation_result(result_transform, input.pivot);
}

ViewportNavigationResult viewport_fly_move(const ViewportFlyMoveInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_fly_move");
	if (geometry_is_tiny(input.local_direction) || std::abs(input.distance) <= 0.000001) {
		return navigation_result(input.camera_transform, input.pivot);
	}
	quader_poly::Vec3 world_direction =
			geometry_scale(viewport_camera_right(input.camera_transform), input.local_direction.x) +
			geometry_scale(viewport_camera_up(input.camera_transform), input.local_direction.y) +
			geometry_scale(viewport_camera_forward(input.camera_transform), input.local_direction.z);
	if (geometry_is_tiny(world_direction)) {
		return navigation_result(input.camera_transform, input.pivot);
	}
	const quader_poly::Vec3 move = geometry_scale(geometry_normalized_axis(world_direction), input.distance);
	quader_poly::Transform3 result_transform = input.camera_transform;
	result_transform.origin += move;
	return navigation_result(result_transform, input.pivot + move);
}

ViewportNavigationResult viewport_frame_bounds(const ViewportFrameBoundsInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_frame_bounds");
	const quader_poly::Vec3 center = geometry_scale(input.min_bounds + input.max_bounds, 0.5);
	const quader_poly::Vec3 extents = geometry_scale(input.max_bounds - input.min_bounds, 0.5);
	double radius = geometry_length(extents);
	if (radius <= 0.0001) {
		radius = 1.0;
	}

	const double margin = std::max(1.0, input.margin);
	quader_poly::Transform3 result_transform = input.camera.transform;
	if (input.camera.projection == CameraProjection::orthographic) {
		const double aspect = viewport_aspect_for_size(input.camera.viewport_size);
		const double needed_height = std::max(static_cast<double>(extents.y) * 2.0, (static_cast<double>(extents.x) * 2.0) / aspect);
		const double size = std::clamp(std::max(needed_height, radius * 2.0) * margin, navigation_min_orthographic_size, navigation_max_orthographic_size);
		result_transform.origin = center - geometry_scale(viewport_camera_forward(input.camera.transform), std::max(navigation_default_frame_distance, radius * 2.0));
		return navigation_result(result_transform, center, size);
	}

	const double aspect = viewport_aspect_for_size(input.camera.viewport_size);
	const double vertical_fov = viewport_vertical_fov_radians(input.camera.fov_degrees);
	const double horizontal_fov = 2.0 * std::atan(std::tan(vertical_fov * 0.5) * aspect);
	const double fitting_fov = std::max(0.01, std::min(vertical_fov, horizontal_fov));
	const double distance = std::max(navigation_min_camera_distance, (radius * margin) / std::sin(fitting_fov * 0.5));
	result_transform.origin = center - geometry_scale(viewport_camera_forward(input.camera.transform), distance);
	return navigation_result(result_transform, center);
}

ViewportNavigationResult viewport_view_axis(const ViewportCameraState& camera, quader_poly::Vec3 pivot, ViewAxis axis, int sign)
{
	const quader_poly::Vec3 offset_axis = view_axis_vector(axis, sign);
	double distance = viewport_distance_to_pivot(camera.transform, pivot);
	if (distance <= navigation_min_camera_distance) {
		distance = navigation_default_frame_distance;
	}

	double camera_size = navigation_default_frame_distance;
	if (camera.projection == CameraProjection::orthographic) {
		camera_size = std::clamp(camera.orthographic_size, navigation_min_orthographic_size, navigation_max_orthographic_size);
	} else {
		const double visible_height = 2.0 * distance * std::tan(viewport_vertical_fov_radians(camera.fov_degrees) * 0.5);
		camera_size = std::clamp(visible_height, navigation_min_orthographic_size, navigation_max_orthographic_size);
	}

	quader_poly::Transform3 result_transform = camera.transform;
	result_transform.origin = pivot + geometry_scale(offset_axis, distance);
	const quader_poly::Transform3 basis = orthogonal_camera_basis_for_offset_axis(offset_axis);
	result_transform.x_axis = basis.x_axis;
	result_transform.y_axis = basis.y_axis;
	result_transform.z_axis = basis.z_axis;
	return navigation_result(result_transform, pivot, camera_size);
}

ViewportNavigationResult viewport_step_view_axis(const ViewportCameraState& camera, quader_poly::Vec3 pivot, ViewAxisStepDirection direction)
{
	const AxisTarget current = current_view_axis_target(camera.transform, pivot);
	const AxisTarget target = stepped_axis_target(current, direction);
	return viewport_view_axis(camera, pivot, view_axis_from_index(target.axis_index), target.sign);
}

ViewportInteractionAction plan_viewport_motion(const ViewportInteractionState& state, const ViewportMotionInput& input)
{
	if (state.fly_active) {
		return input.right_down ? ViewportInteractionAction::update_fly_look : ViewportInteractionAction::end_fly;
	}
	if (state.can_begin_fly && input.right_down) {
		return ViewportInteractionAction::begin_fly_and_look;
	}

	if (state.polygon_session) {
		if (state.box_dragging_base) {
			return input.left_down ? ViewportInteractionAction::polygon_update_box_base : ViewportInteractionAction::polygon_cancel_box_base;
		}
		if (state.box_height_preview) {
			return ViewportInteractionAction::polygon_update_box_height;
		}
		if (state.move_drag_active) {
			return input.left_down ? ViewportInteractionAction::update_move_drag : ViewportInteractionAction::commit_move_drag;
		}
		if (state.transform_drag_active) {
			return input.left_down ? ViewportInteractionAction::update_transform_drag : ViewportInteractionAction::commit_transform_drag;
		}
	} else {
		if (state.move_drag_active) {
			return input.left_down ? ViewportInteractionAction::update_move_drag : ViewportInteractionAction::commit_move_drag;
		}
		if (state.transform_drag_active) {
			return input.left_down ? ViewportInteractionAction::update_transform_drag : ViewportInteractionAction::commit_transform_drag;
		}
		if (state.move_candidate_active) {
			if (!input.left_down) {
				return ViewportInteractionAction::clear_move_candidate;
			}
			if (!state.move_candidate_wait_for_drag_threshold || input.move_candidate_drag_distance >= input.move_candidate_drag_threshold) {
				return ViewportInteractionAction::begin_move_candidate_drag;
			}
			return ViewportInteractionAction::wait_move_candidate_drag;
		}
		if (state.selection_box_active) {
			return input.left_down ? ViewportInteractionAction::update_selection_box : ViewportInteractionAction::clear_selection_box;
		}
		if (state.active_tool == ViewportInteractionTool::cut && !state.navigating && !input.middle_down) {
			return ViewportInteractionAction::update_cut_hover;
		}
		if (state.active_tool == ViewportInteractionTool::poly && !state.navigating && !input.middle_down) {
			return ViewportInteractionAction::update_poly_hover;
		}
	}

	if (state.active_tool == ViewportInteractionTool::box && !state.navigating && !input.middle_down) {
		return ViewportInteractionAction::update_box_hover;
	}

	if (state.navigating) {
		if (!input.middle_down) {
			return ViewportInteractionAction::end_navigation;
		}
		if (input.relative_is_zero) {
			return ViewportInteractionAction::consume;
		}
		if (state.navigation_mode == ViewportNavigationMode::pan) {
			return ViewportInteractionAction::update_pan;
		}
		if (state.navigation_mode == ViewportNavigationMode::orbit) {
			return ViewportInteractionAction::update_orbit;
		}
		return ViewportInteractionAction::consume;
	}
	if (input.middle_down) {
		return ViewportInteractionAction::begin_middle_navigation;
	}
	return ViewportInteractionAction::update_hover;
}

ViewportInteractionAction plan_viewport_button(const ViewportInteractionState& state, const ViewportButtonInput& input)
{
	if (input.button == ViewportMouseButton::right) {
		if (input.pressed) {
			return state.can_begin_fly ? ViewportInteractionAction::begin_fly_and_look : ViewportInteractionAction::none;
		}
		return ViewportInteractionAction::end_fly;
	}
	if (state.fly_active) {
		return ViewportInteractionAction::consume;
	}
	if (input.button == ViewportMouseButton::middle) {
		return input.pressed ? ViewportInteractionAction::begin_middle_navigation : ViewportInteractionAction::end_navigation;
	}
	if (input.pressed && (input.button == ViewportMouseButton::wheel_up || input.button == ViewportMouseButton::wheel_down)) {
		if (!state.polygon_session && state.move_drag_active && state.move_drag_tool == ModalMoveDragTool::extrude && state.extrude_tool_active) {
			return ViewportInteractionAction::adjust_extrude_cuts;
		}
		return ViewportInteractionAction::zoom;
	}
	if (input.button != ViewportMouseButton::left) {
		return ViewportInteractionAction::none;
	}

	if (state.polygon_session) {
		if (input.pressed) {
			if (state.active_tool == ViewportInteractionTool::box) {
				return state.box_height_preview ? ViewportInteractionAction::polygon_commit_box_height : ViewportInteractionAction::polygon_begin_box_base;
			}
			if (state.active_tool == ViewportInteractionTool::move && state.has_selection) {
				return ViewportInteractionAction::polygon_begin_move_drag;
			}
			if ((state.active_tool == ViewportInteractionTool::rotate || state.active_tool == ViewportInteractionTool::scale) && state.has_selection) {
				return ViewportInteractionAction::polygon_begin_transform_drag;
			}
			return ViewportInteractionAction::polygon_pick_select;
		}
		if (state.box_dragging_base) {
			return ViewportInteractionAction::polygon_begin_box_height;
		}
		if (state.move_drag_active) {
			return ViewportInteractionAction::commit_move_drag;
		}
		if (state.transform_drag_active) {
			return ViewportInteractionAction::commit_transform_drag;
		}
		return ViewportInteractionAction::none;
	}

	if (state.active_tool == ViewportInteractionTool::select || state.selection_box_active) {
		return ViewportInteractionAction::route_select_button;
	}
	if (state.active_tool == ViewportInteractionTool::cut || state.cut_active) {
		return ViewportInteractionAction::route_cut_button;
	}
	if (state.active_tool == ViewportInteractionTool::poly) {
		return ViewportInteractionAction::route_poly_button;
	}
	if (state.active_tool == ViewportInteractionTool::rotate || state.active_tool == ViewportInteractionTool::scale || state.transform_drag_active) {
		return ViewportInteractionAction::route_transform_button;
	}
	if (state.active_tool == ViewportInteractionTool::box || state.box_active) {
		return ViewportInteractionAction::route_box_button;
	}
	return ViewportInteractionAction::route_move_button;
}

ViewCubePrimitives build_view_cube_primitives(const ViewCubePrimitiveInput& input)
{
	const double widget_size = std::clamp(input.widget_size, view_cube_min_size, view_cube_max_size);
	const quader_poly::Vec2 center = view_cube_center(input.viewport_size, input.margin);
	const double cube_scale = widget_size * view_cube_half_factor;
	const double marker_radius = widget_size * view_cube_axis_factor;

	ViewCubePrimitives primitives;
	primitives.ok = true;
	primitives.screen_position = center;
	primitives.rect_position = {
		static_cast<float>(static_cast<double>(center.x) - widget_size * 0.5),
		static_cast<float>(static_cast<double>(center.y) - widget_size * 0.5),
	};
	primitives.rect_size = { static_cast<float>(widget_size), static_cast<float>(widget_size) };
	primitives.faces = build_view_cube_faces_for_state(input.camera_transform, center, cube_scale);
	primitives.edges = build_view_cube_edges_for_state(input.camera_transform, center, cube_scale);
	primitives.axis_edges = build_view_cube_axis_edges_for_state(input.camera_transform, center, cube_scale);
	primitives.axis_markers = build_view_cube_markers_for_state(input.camera_transform, center, marker_radius);
	return primitives;
}

ViewCubeHit pick_view_cube(const ViewCubePickInput& input)
{
	const double widget_size = std::clamp(input.primitives.widget_size, view_cube_min_size, view_cube_max_size);
	const quader_poly::Vec2 center = view_cube_center(input.primitives.viewport_size, input.primitives.margin);
	const double radius = widget_size * 0.5;
	const quader_poly::Vec2 from_center = vec2_sub(input.screen_position, center);

	ViewCubeHit hit;
	hit.ok = true;
	hit.hit = false;
	if (vec2_dot(from_center, from_center) > radius * radius) {
		return hit;
	}

	int best_face_index = -1;
	double best_depth = -1000000.0;
	const double cube_scale = widget_size * view_cube_half_factor;
	const std::vector<ViewCubeFacePrimitive> faces = build_view_cube_faces_for_state(input.primitives.camera_transform, center, cube_scale);
	for (int index = 0; index < static_cast<int>(faces.size()); ++index) {
		if (!point_in_polygon(faces[index].polygon, input.screen_position)) {
			continue;
		}
		if (faces[index].depth > best_depth) {
			best_depth = faces[index].depth;
			best_face_index = index;
		}
	}

	hit.hit = true;
	if (best_face_index >= 0) {
		const ViewCubeFacePrimitive& face = faces[best_face_index];
		hit.kind = ViewCubeHitKind::face;
		hit.axis = face.axis;
		hit.sign = face.sign;
		hit.depth = face.depth;
		hit.screen_position = face.screen_position;
		return hit;
	}

	hit.kind = ViewCubeHitKind::body;
	hit.screen_position = center;
	return hit;
}

bool view_cube_hits_match(const ViewCubeHit& left, const ViewCubeHit& right)
{
	return left.hit == right.hit &&
		left.kind == right.kind &&
		left.axis == right.axis &&
		left.sign == right.sign;
}

} // namespace quader_editor_core

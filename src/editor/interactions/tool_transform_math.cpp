////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/tool_transform_math.h>

#include "tool_math_internal.h"

#include <algorithm>
#include <cmath>

namespace quader_editor_core {
namespace {

double determinant(const quader_poly::Transform3& transform)
{
	return vec3_dot(transform.x_axis, vec3_cross(transform.y_axis, transform.z_axis));
}

quader_poly::Vec3 rotate_vector_around_axis(quader_poly::Vec3 value, quader_poly::Vec3 axis, double angle_radians)
{
	const quader_poly::Vec3 normal = normalized_axis(axis);
	if (vec3_is_tiny(normal)) {
		return value;
	}
	const double c = std::cos(angle_radians);
	const double s = std::sin(angle_radians);
	const double one_minus_c = 1.0 - c;
	return vec3_scale(value, c) +
		vec3_scale(vec3_cross(normal, value), s) +
		vec3_scale(normal, vec3_dot(normal, value) * one_minus_c);
}

double base_scale_distance(TransformAxis axis, quader_poly::Vec3 bounds_min, quader_poly::Vec3 bounds_max, quader_poly::Vec3 pivot)
{
	const auto max_abs = [](double left, double right) {
		return std::max(std::abs(left), std::abs(right));
	};
	switch (axis) {
		case TransformAxis::x:
			return max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x);
		case TransformAxis::y:
			return max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y);
		case TransformAxis::z:
			return max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z);
		case TransformAxis::xy:
			return std::max(
					max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x),
					max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y));
		case TransformAxis::xz:
			return std::max(
					max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x),
					max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z));
		case TransformAxis::yz:
			return std::max(
					max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y),
					max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z));
		case TransformAxis::all:
			return std::max(
					std::max(
							max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x),
							max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y)),
					max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z));
		case TransformAxis::none:
		default:
			return 0.0;
	}
}

bool axis_includes_component(TransformAxis axis, int component)
{
	if (axis == TransformAxis::all) {
		return true;
	}
	if (component == 0) {
		return axis == TransformAxis::x || axis == TransformAxis::xy || axis == TransformAxis::xz;
	}
	if (component == 1) {
		return axis == TransformAxis::y || axis == TransformAxis::xy || axis == TransformAxis::yz;
	}
	if (component == 2) {
		return axis == TransformAxis::z || axis == TransformAxis::xz || axis == TransformAxis::yz;
	}
	return false;
}

double bounds_extent_component(quader_poly::Vec3 bounds_min, quader_poly::Vec3 bounds_max, int component)
{
	if (component == 0) {
		return std::abs(bounds_max.x - bounds_min.x);
	}
	if (component == 1) {
		return std::abs(bounds_max.y - bounds_min.y);
	}
	if (component == 2) {
		return std::abs(bounds_max.z - bounds_min.z);
	}
	return 0.0;
}

double minimum_scale_factor_for_grid(TransformAxis axis, quader_poly::Vec3 bounds_min, quader_poly::Vec3 bounds_max, double grid_size, bool shrinking)
{
	double factor = min_scale_factor;
	for (int component = 0; component < 3; ++component) {
		if (!axis_includes_component(axis, component)) {
			continue;
		}
		const double extent = bounds_extent_component(bounds_min, bounds_max, component);
		if (extent <= screen_epsilon) {
			continue;
		}
		double axis_factor = grid_size / extent;
		if (shrinking && axis_factor > 1.0) {
			axis_factor = 1.0;
		}
		factor = std::max(factor, axis_factor);
	}
	return factor;
}

} // namespace

quader_poly::Vec3 axis_vector(TransformAxis axis)
{
	switch (axis) {
		case TransformAxis::x:
			return { 1.0F, 0.0F, 0.0F };
		case TransformAxis::y:
			return { 0.0F, 1.0F, 0.0F };
		case TransformAxis::z:
			return { 0.0F, 0.0F, 1.0F };
		case TransformAxis::xy:
			return { 1.0F, 1.0F, 0.0F };
		case TransformAxis::xz:
			return { 1.0F, 0.0F, 1.0F };
		case TransformAxis::yz:
			return { 0.0F, 1.0F, 1.0F };
		case TransformAxis::all:
			return { 1.0F, 1.0F, 1.0F };
		case TransformAxis::none:
		default:
			return {};
	}
}

quader_poly::Vec3 rotation_axis_vector(TransformAxis axis)
{
	switch (axis) {
		case TransformAxis::x:
			return { 1.0F, 0.0F, 0.0F };
		case TransformAxis::y:
			return { 0.0F, 1.0F, 0.0F };
		case TransformAxis::z:
			return { 0.0F, 0.0F, 1.0F };
		case TransformAxis::none:
		case TransformAxis::xy:
		case TransformAxis::xz:
		case TransformAxis::yz:
		case TransformAxis::all:
		default:
			return {};
	}
}

quader_poly::Vec3 axis_scale(TransformAxis axis, double factor)
{
	quader_poly::Vec3 scale { 1.0F, 1.0F, 1.0F };
	switch (axis) {
		case TransformAxis::x:
			scale.x = static_cast<float>(factor);
			break;
		case TransformAxis::y:
			scale.y = static_cast<float>(factor);
			break;
		case TransformAxis::z:
			scale.z = static_cast<float>(factor);
			break;
		case TransformAxis::xy:
			scale.x = static_cast<float>(factor);
			scale.y = static_cast<float>(factor);
			break;
		case TransformAxis::xz:
			scale.x = static_cast<float>(factor);
			scale.z = static_cast<float>(factor);
			break;
		case TransformAxis::yz:
			scale.y = static_cast<float>(factor);
			scale.z = static_cast<float>(factor);
			break;
		case TransformAxis::all:
			scale = { static_cast<float>(factor), static_cast<float>(factor), static_cast<float>(factor) };
			break;
		case TransformAxis::none:
		default:
			break;
	}
	return scale;
}

quader_poly::Vec3 transform_vector(const quader_poly::Transform3& transform, quader_poly::Vec3 value)
{
	return {
		transform.x_axis.x * value.x + transform.y_axis.x * value.y + transform.z_axis.x * value.z,
		transform.x_axis.y * value.x + transform.y_axis.y * value.y + transform.z_axis.y * value.z,
		transform.x_axis.z * value.x + transform.y_axis.z * value.y + transform.z_axis.z * value.z,
	};
}

quader_poly::Vec3 transform_point(const quader_poly::Transform3& transform, quader_poly::Vec3 point)
{
	return transform_vector(transform, point) + transform.origin;
}

quader_poly::Vec3 inverse_transform_vector(const quader_poly::Transform3& transform, quader_poly::Vec3 value)
{
	const double det = determinant(transform);
	if (std::abs(det) <= tool_epsilon) {
		return {};
	}
	return {
		static_cast<float>(vec3_dot(value, vec3_cross(transform.y_axis, transform.z_axis)) / det),
		static_cast<float>(vec3_dot(value, vec3_cross(transform.z_axis, transform.x_axis)) / det),
		static_cast<float>(vec3_dot(value, vec3_cross(transform.x_axis, transform.y_axis)) / det),
	};
}

quader_poly::Vec3 inverse_transform_point(const quader_poly::Transform3& transform, quader_poly::Vec3 point)
{
	return inverse_transform_vector(transform, point - transform.origin);
}

quader_poly::Transform3 pivoted_transform(const quader_poly::Transform3& transform, quader_poly::Vec3 pivot)
{
	quader_poly::Transform3 result = transform;
	result.origin = pivot + transform.origin - transform_vector(transform, pivot);
	return result;
}

quader_poly::Transform3 rotation_transform(TransformAxis axis, double angle_radians)
{
	const quader_poly::Vec3 axis_value = rotation_axis_vector(axis);
	if (vec3_is_tiny(axis_value)) {
		return {};
	}
	return {
		rotate_vector_around_axis({ 1.0F, 0.0F, 0.0F }, axis_value, angle_radians),
		rotate_vector_around_axis({ 0.0F, 1.0F, 0.0F }, axis_value, angle_radians),
		rotate_vector_around_axis({ 0.0F, 0.0F, 1.0F }, axis_value, angle_radians),
		{},
	};
}

quader_poly::Transform3 scale_transform(quader_poly::Vec3 scale)
{
	return {
		{ scale.x, 0.0F, 0.0F },
		{ 0.0F, scale.y, 0.0F },
		{ 0.0F, 0.0F, scale.z },
		{},
	};
}

double scale_delta(quader_poly::Vec3 scale)
{
	return std::abs(scale.x - 1.0) + std::abs(scale.y - 1.0) + std::abs(scale.z - 1.0);
}

double wrap_radians(double radians)
{
	const double tau = std::acos(-1.0) * 2.0;
	double value = std::fmod(radians + std::acos(-1.0), tau);
	if (value < 0.0) {
		value += tau;
	}
	return value - std::acos(-1.0);
}

double snap_move_distance(double distance, bool precision, bool grid_enabled, double grid_size)
{
	double snapped = precision ? distance * 0.25 : distance;
	if (grid_enabled) {
		const double grid = safe_grid_size(grid_size, tool_epsilon);
		snapped = std::round(snapped / grid) * grid;
	}
	return snapped;
}

double snapped_scale_factor(TransformAxis axis, double factor, const ToolBounds3& bounds, quader_poly::Vec3 pivot, bool grid_enabled, double grid_size)
{
	if (!bounds.has_bounds || !grid_enabled || grid_size <= screen_epsilon) {
		return factor;
	}
	if (std::abs(factor - 1.0) <= tool_epsilon) {
		return 1.0;
	}
	const double base_distance_value = base_scale_distance(axis, bounds.min, bounds.max, pivot);
	if (base_distance_value <= screen_epsilon) {
		return factor;
	}
	const double base_extent = base_distance_value * 2.0;
	const double raw_extent = std::max(tool_epsilon, base_extent * factor);
	const double grid = safe_grid_size(grid_size, tool_epsilon);
	const bool shrinking = factor < 1.0;
	const double snapped_units = shrinking ? std::floor(raw_extent / grid) : std::round(raw_extent / grid);
	const double snapped_extent = snapped_units * grid;
	const double snapped_factor = snapped_extent <= screen_epsilon ? min_scale_factor : snapped_extent / base_extent;
	const double minimum_factor = minimum_scale_factor_for_grid(axis, bounds.min, bounds.max, grid, shrinking);
	return std::max(minimum_factor, snapped_factor);
}

} // namespace quader_editor_core

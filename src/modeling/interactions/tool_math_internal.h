////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <modeling/interactions/interaction_api.h>
#include <geometry/geometry.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace quader_editor_core {

static inline quader_geometry::Vec2d tool_geometry_vec2(quader_poly::Vec2 value)
{
	return { value.x, value.y };
}

static inline quader_geometry::Vec3f tool_geometry_vec3(quader_poly::Vec3 value)
{
	return { value.x, value.y, value.z };
}

static inline quader_poly::Vec2 tool_poly_vec2(quader_geometry::Vec2d value)
{
	return { static_cast<float>(value.x), static_cast<float>(value.y) };
}

static inline quader_poly::Vec3 tool_poly_vec3(quader_geometry::Vec3f value)
{
	return { value.x, value.y, value.z };
}

static inline quader_poly::Vec2 vec2_sub(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return { left.x - right.x, left.y - right.y };
}

static inline quader_poly::Vec2 vec2_add(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return { left.x + right.x, left.y + right.y };
}

static inline quader_poly::Vec2 vec2_scale(quader_poly::Vec2 value, double scalar)
{
	return tool_poly_vec2(tool_geometry_vec2(value) * scalar);
}

static inline double vec2_dot(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return quader_geometry::dot(tool_geometry_vec2(left), tool_geometry_vec2(right));
}

static inline double vec2_distance_squared(quader_poly::Vec2 left, quader_poly::Vec2 right)
{
	return quader_geometry::length_squared(tool_geometry_vec2(left) - tool_geometry_vec2(right));
}

static inline bool vec2_is_tiny(quader_poly::Vec2 value)
{
	return quader_geometry::length_squared(tool_geometry_vec2(value)) <= screen_epsilon;
}

static inline quader_poly::Vec2 vec2_normalize_or(quader_poly::Vec2 value, quader_poly::Vec2 fallback)
{
	const quader_geometry::Vec2d geometry_value = tool_geometry_vec2(value);
	const double length_squared = quader_geometry::length_squared(geometry_value);
	if (length_squared <= screen_epsilon) {
		return fallback;
	}
	const double length = std::sqrt(length_squared);
	return tool_poly_vec2(geometry_value / length);
}

static inline bool vec3_is_tiny(quader_poly::Vec3 value, double epsilon = tool_epsilon)
{
	return quader_geometry::length_squared(tool_geometry_vec3(value)) <= static_cast<float>(epsilon * epsilon);
}

static inline quader_poly::Vec3 vec3_scale(quader_poly::Vec3 value, double scalar)
{
	return tool_poly_vec3(tool_geometry_vec3(value) * static_cast<float>(scalar));
}

static inline quader_poly::Vec3 vec3_cross(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return tool_poly_vec3(quader_geometry::cross(tool_geometry_vec3(left), tool_geometry_vec3(right)));
}

static inline double vec3_dot(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return quader_geometry::dot(tool_geometry_vec3(left), tool_geometry_vec3(right));
}

static inline quader_poly::Vec3 normalized_axis(quader_poly::Vec3 value)
{
	return tool_poly_vec3(quader_geometry::normalize_or_zero(tool_geometry_vec3(value), 0.000001F));
}

static inline double safe_grid_size(double grid_size, double minimum)
{
	return std::max(grid_size, minimum);
}

static inline bool int_vector_contains(const std::vector<int>& values, int value)
{
	return std::find(values.begin(), values.end(), value) != values.end();
}

static inline void append_unique_id(std::vector<int>& values, int value)
{
	if (value < 0 || int_vector_contains(values, value)) {
		return;
	}
	values.push_back(value);
}

static inline std::vector<int> merge_unique_ids(const std::vector<int>& first, const std::vector<int>& second)
{
	std::vector<int> result;
	result.reserve(first.size() + second.size());
	for (const int id : first) {
		append_unique_id(result, id);
	}
	for (const int id : second) {
		append_unique_id(result, id);
	}
	return result;
}

static inline double vec3_distance_squared(quader_poly::Vec3 left, quader_poly::Vec3 right)
{
	return quader_geometry::length_squared(tool_geometry_vec3(left) - tool_geometry_vec3(right));
}

double viewport_world_units_per_pixel(const ViewportCameraState& camera, quader_poly::Vec3 anchor);

} // namespace quader_editor_core

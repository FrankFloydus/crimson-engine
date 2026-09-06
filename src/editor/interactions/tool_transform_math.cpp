////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/tool_transform_math.hpp>

#include "tool_math_internal.hpp"

#include <algorithm>
#include <cmath>

namespace quader_editor_core {
namespace {

double determinant(const quader_poly::Transform3& transform)
{
	return vec3_dot(transform.x_axis, vec3_cross(transform.y_axis, transform.z_axis));
}

bool linear_basis_is_identity(const quader_poly::Transform3& transform)
{
	return transform.x_axis.x == 1.0F && transform.x_axis.y == 0.0F && transform.x_axis.z == 0.0F &&
		transform.y_axis.x == 0.0F && transform.y_axis.y == 1.0F && transform.y_axis.z == 0.0F &&
		transform.z_axis.x == 0.0F && transform.z_axis.y == 0.0F && transform.z_axis.z == 1.0F;
}

quader_poly::Transform3 conjugated_linear_transform(
		const quader_poly::Transform3& local_transform,
		const quader_poly::Transform3& orientation)
{
	if (linear_basis_is_identity(orientation) ||
		std::abs(determinant(orientation)) <= kToolEpsilon) {
		return local_transform;
	}

	const auto transform_world_direction = [&local_transform, &orientation](quader::QVec3 direction) {
		return transform_vector(
			orientation,
			transform_vector(
				local_transform,
				inverse_transform_vector(orientation, direction)));
	};
	return {
		transform_world_direction({1.0F, 0.0F, 0.0F}),
		transform_world_direction({0.0F, 1.0F, 0.0F}),
		transform_world_direction({0.0F, 0.0F, 1.0F}),
		{},
	};
}

quader::QVec3 rotate_vector_around_axis(quader::QVec3 value, quader::QVec3 axis, double angle_radians)
{
	const quader::QVec3 normal = normalized_axis(axis);
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

double base_scale_distance(TransformAxis axis, quader::QVec3 bounds_min, quader::QVec3 bounds_max, quader::QVec3 pivot)
{
	const auto max_abs = [](double left, double right) {
		return std::max(std::abs(left), std::abs(right));
	};
	switch (axis) {
        case TransformAxis::X:
          return max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x);
        case TransformAxis::Y:
          return max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y);
        case TransformAxis::Z:
          return max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z);
        case TransformAxis::Xy:
          return std::max(
              max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x),
              max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y));
        case TransformAxis::Xz:
          return std::max(
              max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x),
              max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z));
        case TransformAxis::Yz:
          return std::max(
              max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y),
              max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z));
        case TransformAxis::All:
          return std::max(
              std::max(max_abs(bounds_max.x - pivot.x, bounds_min.x - pivot.x),
                       max_abs(bounds_max.y - pivot.y, bounds_min.y - pivot.y)),
              max_abs(bounds_max.z - pivot.z, bounds_min.z - pivot.z));
        case TransformAxis::None:
        default:
          return 0.0;
	}
}

bool axis_includes_component(TransformAxis axis, int component)
{
  if (axis == TransformAxis::All) {
    return true;
  }
        if (component == 0) {
          return axis == TransformAxis::X || axis == TransformAxis::Xy ||
                 axis == TransformAxis::Xz;
        }
	if (component == 1) {
          return axis == TransformAxis::Y || axis == TransformAxis::Xy ||
                 axis == TransformAxis::Yz;
        }
	if (component == 2) {
          return axis == TransformAxis::Z || axis == TransformAxis::Xz ||
                 axis == TransformAxis::Yz;
        }
	return false;
}

double bounds_extent_component(quader::QVec3 bounds_min, quader::QVec3 bounds_max, int component)
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

double minimum_scale_factor_for_grid(TransformAxis axis, quader::QVec3 bounds_min, quader::QVec3 bounds_max, double grid_size, bool shrinking)
{
  double factor = kMinScaleFactor;
  for (int component = 0; component < 3; ++component) {
    if (!axis_includes_component(axis, component)) {
      continue;
    }
    const double extent =
        bounds_extent_component(bounds_min, bounds_max, component);
    if (extent <= kScreenEpsilon) {
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

quader::QVec3 axis_vector(TransformAxis axis)
{
	switch (axis) {
        case TransformAxis::X:
          return {1.0F, 0.0F, 0.0F};
        case TransformAxis::Y:
          return {0.0F, 1.0F, 0.0F};
        case TransformAxis::Z:
          return {0.0F, 0.0F, 1.0F};
        case TransformAxis::Xy:
          return {1.0F, 1.0F, 0.0F};
        case TransformAxis::Xz:
          return {1.0F, 0.0F, 1.0F};
        case TransformAxis::Yz:
          return {0.0F, 1.0F, 1.0F};
        case TransformAxis::All:
          return {1.0F, 1.0F, 1.0F};
        case TransformAxis::None:
        default:
          return {};
	}
}

quader::QVec3 rotation_axis_vector(TransformAxis axis)
{
	switch (axis) {
        case TransformAxis::X:
          return {1.0F, 0.0F, 0.0F};
        case TransformAxis::Y:
          return {0.0F, 1.0F, 0.0F};
        case TransformAxis::Z:
          return {0.0F, 0.0F, 1.0F};
        case TransformAxis::None:
        case TransformAxis::Xy:
        case TransformAxis::Xz:
        case TransformAxis::Yz:
        case TransformAxis::All:
        default:
          return {};
	}
}

quader::QVec3 axis_scale(TransformAxis axis, double factor)
{
	quader::QVec3 scale { 1.0F, 1.0F, 1.0F };
	switch (axis) {
        case TransformAxis::X:
          scale.x = static_cast<float>(factor);
          break;
        case TransformAxis::Y:
          scale.y = static_cast<float>(factor);
          break;
        case TransformAxis::Z:
          scale.z = static_cast<float>(factor);
          break;
        case TransformAxis::Xy:
          scale.x = static_cast<float>(factor);
          scale.y = static_cast<float>(factor);
          break;
        case TransformAxis::Xz:
          scale.x = static_cast<float>(factor);
          scale.z = static_cast<float>(factor);
          break;
        case TransformAxis::Yz:
          scale.y = static_cast<float>(factor);
          scale.z = static_cast<float>(factor);
          break;
        case TransformAxis::All:
          scale = {static_cast<float>(factor), static_cast<float>(factor),
                   static_cast<float>(factor)};
          break;
        case TransformAxis::None:
        default:
          break;
	}
	return scale;
}

quader::QVec3 transform_vector(const quader_poly::Transform3& transform, quader::QVec3 value)
{
	return {
		transform.x_axis.x * value.x + transform.y_axis.x * value.y + transform.z_axis.x * value.z,
		transform.x_axis.y * value.x + transform.y_axis.y * value.y + transform.z_axis.y * value.z,
		transform.x_axis.z * value.x + transform.y_axis.z * value.y + transform.z_axis.z * value.z,
	};
}

quader::QVec3 transform_point(const quader_poly::Transform3& transform, quader::QVec3 point)
{
	return transform_vector(transform, point) + transform.origin;
}

quader::QVec3 inverse_transform_vector(const quader_poly::Transform3& transform, quader::QVec3 value)
{
	const double det = determinant(transform);
        if (std::abs(det) <= kToolEpsilon) {
          return {};
        }
        return {
		static_cast<float>(vec3_dot(value, vec3_cross(transform.y_axis, transform.z_axis)) / det),
		static_cast<float>(vec3_dot(value, vec3_cross(transform.z_axis, transform.x_axis)) / det),
		static_cast<float>(vec3_dot(value, vec3_cross(transform.x_axis, transform.y_axis)) / det),
	};
}

quader::QVec3 inverse_transform_point(const quader_poly::Transform3& transform, quader::QVec3 point)
{
	return inverse_transform_vector(transform, point - transform.origin);
}

quader_poly::Transform3 pivoted_transform(const quader_poly::Transform3& transform, quader::QVec3 pivot)
{
	quader_poly::Transform3 result = transform;
	result.origin = pivot + transform.origin - transform_vector(transform, pivot);
	return result;
}

quader_poly::Transform3 rotation_transform(TransformAxis axis, double angle_radians)
{
	return rotation_transform(rotation_axis_vector(axis), angle_radians);
}

quader_poly::Transform3 rotation_transform(quader::QVec3 axis, double angle_radians)
{
	if (vec3_is_tiny(axis)) {
		return {};
	}
	return {
		rotate_vector_around_axis({ 1.0F, 0.0F, 0.0F }, axis, angle_radians),
		rotate_vector_around_axis({ 0.0F, 1.0F, 0.0F }, axis, angle_radians),
		rotate_vector_around_axis({ 0.0F, 0.0F, 1.0F }, axis, angle_radians),
		{},
	};
}

quader_poly::Transform3 scale_transform(quader::QVec3 scale)
{
	return {
		{ scale.x, 0.0F, 0.0F },
		{ 0.0F, scale.y, 0.0F },
		{ 0.0F, 0.0F, scale.z },
		{},
	};
}

quader_poly::Transform3 oriented_rotation_transform(
		TransformAxis axis,
		double angle_radians,
		const quader_poly::Transform3& orientation)
{
	return conjugated_linear_transform(
		rotation_transform(axis, angle_radians), orientation);
}

quader_poly::Transform3 oriented_scale_transform(
		quader::QVec3 scale,
		const quader_poly::Transform3& orientation)
{
	return conjugated_linear_transform(scale_transform(scale), orientation);
}

double scale_delta(quader::QVec3 scale)
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
          const double grid = safe_grid_size(grid_size, kToolEpsilon);
          snapped = std::round(snapped / grid) * grid;
	}
	return snapped;
}

double snapped_scale_factor(TransformAxis axis, double factor, const ToolBounds3& bounds, quader::QVec3 pivot, bool grid_enabled, double grid_size)
{
  if (!bounds.has_bounds || !grid_enabled || grid_size <= kScreenEpsilon) {
    return factor;
  }
  if (std::abs(factor - 1.0) <= kToolEpsilon) {
    return 1.0;
  }
        const double base_distance_value = base_scale_distance(axis, bounds.min, bounds.max, pivot);
        if (base_distance_value <= kScreenEpsilon) {
          return factor;
        }
        const double base_extent = base_distance_value * 2.0;
        const double raw_extent = std::max(kToolEpsilon, base_extent * factor);
        const double grid = safe_grid_size(grid_size, kToolEpsilon);
        const bool shrinking = factor < 1.0;
	const double snapped_units = shrinking ? std::floor(raw_extent / grid) : std::round(raw_extent / grid);
	// Grid snapping quantizes dimensions while at least one full grid interval
	// remains. It must not turn that interval into a hard minimum: freshly
	// created primitives commonly start exactly one grid cell wide, and the
	// uniform center handle still needs to scale them smoothly below that size.
	if (shrinking && snapped_units < 1.0) {
		return std::max(kMinScaleFactor, factor);
	}
	const double snapped_extent = snapped_units * grid;
        const double snapped_factor = snapped_extent <= kScreenEpsilon
                                          ? kMinScaleFactor
                                          : snapped_extent / base_extent;
	const double minimum_factor =
		shrinking
			? kMinScaleFactor
			: std::max(
				  1.0,
				  minimum_scale_factor_for_grid(
					  axis, bounds.min, bounds.max, grid, false));
	return std::max(minimum_factor, snapped_factor);
}

} // namespace quader_editor_core

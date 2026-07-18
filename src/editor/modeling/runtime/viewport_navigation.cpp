////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/viewport_navigation.hpp>
#include <diagnostics/profile.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace quader_editor_core {
namespace {

quader_geometry::QVec3f geometry_vec3(quader::QVec3 value)
{
	return { value.x, value.y, value.z };
}

quader::QVec3 poly_vec3(quader_geometry::QVec3f value)
{
	return { value.x, value.y, value.z };
}

quader_geometry::QVec2d geometry_vec2(quader::QVec2 value)
{
	return { value.x, value.y };
}

quader::QVec2 poly_vec2(quader_geometry::QVec2d value)
{
	return { static_cast<float>(value.x), static_cast<float>(value.y) };
}

quader::QVec2 vec2_sub(quader::QVec2 left, quader::QVec2 right)
{
	return { left.x - right.x, left.y - right.y };
}

double vec2_dot(quader::QVec2 left, quader::QVec2 right)
{
	return quader_geometry::dot(geometry_vec2(left), geometry_vec2(right));
}

bool vec2_is_tiny(quader::QVec2 value)
{
  return quader_geometry::length_squared(geometry_vec2(value)) <=
         kScreenEpsilon;
}

quader::QVec2 vec2_normalize_or(quader::QVec2 value, quader::QVec2 fallback)
{
	const quader_geometry::QVec2d geometry_value = geometry_vec2(value);
	const double length_squared = quader_geometry::length_squared(geometry_value);
        if (length_squared <= kScreenEpsilon) {
          return fallback;
        }
        const double length = std::sqrt(length_squared);
	return poly_vec2(geometry_value / length);
}

double geometry_dot(quader::QVec3 left, quader::QVec3 right)
{
	return quader_geometry::dot(geometry_vec3(left), geometry_vec3(right));
}

quader::QVec3 geometry_cross(quader::QVec3 left, quader::QVec3 right)
{
	return poly_vec3(quader_geometry::cross(geometry_vec3(left), geometry_vec3(right)));
}

double geometry_length_squared(quader::QVec3 value)
{
	return quader_geometry::length_squared(geometry_vec3(value));
}

double geometry_length(quader::QVec3 value)
{
	return quader_geometry::length(geometry_vec3(value));
}

quader::QVec3 geometry_scale(quader::QVec3 value, double scalar)
{
	return poly_vec3(geometry_vec3(value) * static_cast<float>(scalar));
}

quader::QVec3 geometry_normalized_axis(quader::QVec3 value)
{
  return poly_vec3(quader_geometry::normalize_or_zero(
      geometry_vec3(value), static_cast<float>(kToolEpsilon)));
}

quader::QVec3 geometry_transform_vector(const quader_poly::Transform3& transform, quader::QVec3 value)
{
	return poly_vec3(
			geometry_vec3(transform.x_axis) * value.x +
			geometry_vec3(transform.y_axis) * value.y +
			geometry_vec3(transform.z_axis) * value.z);
}

bool geometry_is_tiny(quader::QVec3 value, double epsilon = kToolEpsilon) {
  return geometry_length_squared(value) <= epsilon * epsilon;
}

quader::QVec3 rotate_vector_around_axis(quader::QVec3 value, quader::QVec3 axis, double angle_radians)
{
	const quader::QVec3 normal = geometry_normalized_axis(axis);
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
	quader::QVec3 right = geometry_normalized_axis(basis.x_axis);
	if (geometry_is_tiny(right)) {
		right = { 1.0F, 0.0F, 0.0F };
	}
	quader::QVec3 up = basis.y_axis - geometry_scale(right, geometry_dot(basis.y_axis, right));
	up = geometry_normalized_axis(up);
	if (geometry_is_tiny(up)) {
		const quader::QVec3 fallback = std::abs(right.y) > 0.9 ? quader::QVec3 { 0.0F, 0.0F, 1.0F } : quader::QVec3 { 0.0F, 1.0F, 0.0F };
		up = geometry_normalized_axis(fallback - geometry_scale(right, geometry_dot(fallback, right)));
	}
	quader::QVec3 back = geometry_normalized_axis(geometry_cross(right, up));
	if (geometry_is_tiny(back)) {
		back = { 0.0F, 0.0F, 1.0F };
	}
	up = geometry_normalized_axis(geometry_cross(back, right));
	return { right, up, back, basis.origin };
}

quader_poly::Transform3 rotation_basis(quader::QVec3 axis, double angle_radians)
{
	const quader::QVec3 normal = geometry_normalized_axis(axis);
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
	const quader::QVec3 world_up { 0.0F, 1.0F, 0.0F };
	const quader::QVec3 new_up = geometry_normalized_axis(geometry_transform_vector(rotation, camera_transform.y_axis));
	if (geometry_dot(new_up, world_up) >= 0.0) {
		return orthonormalized_basis(rotation);
	}

	const quader::QVec3 new_right = geometry_normalized_axis(geometry_transform_vector(rotation, camera_transform.x_axis));
	const quader::QVec3 new_forward = geometry_normalized_axis(geometry_transform_vector(rotation, geometry_scale(camera_transform.z_axis, -1.0)));
	quader::QVec3 clamped_up = new_up - geometry_scale(world_up, geometry_dot(new_up, world_up));
	clamped_up = geometry_is_tiny(clamped_up) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : geometry_normalized_axis(clamped_up);

	const double cos_angle = std::clamp(geometry_dot(clamped_up, new_up), -1.0, 1.0);
	const double correction_angle = std::acos(cos_angle);
	const double correction_sign = geometry_dot(new_forward, world_up) > 0.0 ? -1.0 : 1.0;
	const quader_poly::Transform3 correction = rotation_basis(new_right, correction_angle * correction_sign);
	return orthonormalized_basis(basis_multiply(correction, rotation));
}

quader_poly::Transform3 orthogonal_camera_basis_for_offset_axis(quader::QVec3 axis)
{
	const quader::QVec3 world_up { 0.0F, 1.0F, 0.0F };
	const quader::QVec3 back = geometry_is_tiny(axis) ? quader::QVec3 { 0.0F, 0.0F, 1.0F } : geometry_normalized_axis(axis);
	quader::QVec3 right;
	if (std::abs(geometry_dot(back, world_up)) > 0.999) {
		right = { 1.0F, 0.0F, 0.0F };
	} else {
		right = geometry_cross(world_up, back);
	}
	right = geometry_is_tiny(right) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : geometry_normalized_axis(right);
	const quader::QVec3 up = geometry_normalized_axis(geometry_cross(back, right));
	return orthonormalized_basis({ right, geometry_is_tiny(up) ? world_up : up, back, {} });
}

/**
 * Distinguishes snap targets that should enter a locked orthographic projection from diagonal orbit snap targets.
 */
struct ViewportAxisSnapTransformResult {
	bool orthographic_axis = false;
};

/**
 * Returns the chosen camera basis and whether its roll lands on a canonical single-axis orthographic orientation.
 */
struct ViewportAxisSnapBasisResult {
	quader_poly::Transform3 basis;
	bool canonical_axis_view = false;
};

ViewportAxisSnapBasisResult closest_blender_axis_snap_camera_basis(quader::QVec3 axis, int axis_components, const quader_poly::Transform3& reference_basis)
{
	const quader::QVec3 back = geometry_is_tiny(axis) ? quader::QVec3 { 0.0F, 0.0F, 1.0F } : geometry_normalized_axis(axis);
	const quader_poly::Transform3 base = orthogonal_camera_basis_for_offset_axis(back);
	const quader::QVec3 reference_back = geometry_normalized_axis(reference_basis.z_axis);
	const double alignment = std::clamp(geometry_dot(reference_back, back), -1.0, 1.0);
	quader_poly::Transform3 aligned_basis = reference_basis;
	if (alignment < 0.999999) {
		quader::QVec3 alignment_axis = geometry_cross(reference_back, back);
		if (geometry_is_tiny(alignment_axis)) {
			alignment_axis = std::abs(reference_back.y) > 0.9F ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : quader::QVec3 { 0.0F, 1.0F, 0.0F };
		}
		aligned_basis = rotated_camera_basis(reference_basis, rotation_basis(alignment_axis, std::acos(alignment)));
	}

        static constexpr std::array<double, 8> kRolls{
            0.0,
            0.7853981633974483,
            1.5707963267948966,
            2.3561944901923449,
            3.1415926535897932,
            3.9269908169872414,
            4.7123889803846899,
            5.4977871437821380,
        };

        quader_poly::Transform3 best_basis = aligned_basis;
        double best_dot = kNavigationOrthogonalSnapDot;
        int best_roll_index = -1;
        for (int index = 0; index < static_cast<int>(kRolls.size()); ++index) {
          const quader_poly::Transform3 candidate =
              rotated_camera_basis(base, rotation_basis(back, kRolls[index]));
          const double candidate_dot =
              geometry_dot(geometry_normalized_axis(candidate.x_axis),
                           geometry_normalized_axis(aligned_basis.x_axis));
          if (candidate_dot >= best_dot) {
            best_dot = candidate_dot;
            best_basis = candidate;
            best_roll_index = index;
          }
        }

        return {
		orthonormalized_basis(best_basis),
		axis_components == 1 && best_roll_index >= 0 && best_roll_index % 2 == 0,
	};
}

ViewportNavigationResult navigation_result(const quader_poly::Transform3& transform, quader::QVec3 pivot, double camera_size = -1.0)
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
  const double safe_current_size =
      std::max(kNavigationMinOrthographicSize, current_size);
  const double requested_size = safe_current_size / factor;
  if (steps < 0.0) {
    if (safe_current_size >= kNavigationMaxOrthographicZoomOutSize) {
      return safe_current_size;
    }
    return std::clamp(requested_size, kNavigationMinOrthographicSize,
                      kNavigationMaxOrthographicZoomOutSize);
  }
  return std::max(kNavigationMinOrthographicSize, requested_size);
}

ViewportAxisSnapTransformResult axis_snap_transform(const quader_poly::Transform3& unsnapped_transform, quader::QVec3 pivot, quader_poly::Transform3& snapped_transform)
{
	const quader::QVec3 offset = unsnapped_transform.origin - pivot;
	const double distance = geometry_length(offset);
        if (distance <= kNavigationMinCameraDistance) {
          return {};
        }

        const quader::QVec3 direction = geometry_is_tiny(unsnapped_transform.z_axis)
		? offset / static_cast<float>(distance)
		: geometry_normalized_axis(unsnapped_transform.z_axis);
	double best_dot = -1.0;
	quader::QVec3 best_axis { 0.0F, 0.0F, 1.0F };
	int best_axis_components = 1;
	for (int x = -1; x < 2; ++x) {
		for (int y = -1; y < 2; ++y) {
			for (int z = -1; z < 2; ++z) {
				const int component_count = (x != 0 ? 1 : 0) + (y != 0 ? 1 : 0) + (z != 0 ? 1 : 0);
				if (component_count == 0) {
					continue;
				}
				const quader::QVec3 axis = geometry_normalized_axis({
						static_cast<float>(x),
						static_cast<float>(y),
						static_cast<float>(z),
				});
				const double axis_dot = geometry_dot(direction, axis);
				if (axis_dot > best_dot) {
					best_dot = axis_dot;
					best_axis = axis;
					best_axis_components = component_count;
				}
			}
		}
	}
        if (best_dot < kNavigationOrthogonalSnapDot) {
          return {};
        }

        snapped_transform = unsnapped_transform;
	snapped_transform.origin = pivot + geometry_scale(best_axis, distance);
	const ViewportAxisSnapBasisResult snapped_basis =
		closest_blender_axis_snap_camera_basis(best_axis, best_axis_components, unsnapped_transform);
	snapped_transform.x_axis = snapped_basis.basis.x_axis;
	snapped_transform.y_axis = snapped_basis.basis.y_axis;
	snapped_transform.z_axis = snapped_basis.basis.z_axis;
	return { snapped_basis.canonical_axis_view };
}

/**
 * Represents an QAxis Target value used by the modeling runtime domain.
 */
struct AxisTarget {
	int axis_index = 2;
	int sign = 1;
};

AxisTarget axis_target_from_vector(quader::QVec3 value)
{
	value = geometry_is_tiny(value) ? quader::QVec3 { 0.0F, 0.0F, 1.0F } : geometry_normalized_axis(value);
	const quader_geometry::QAxis axis = quader_geometry::dominant_axis(geometry_vec3(value));
	int axis_index = 2;
	double component = value.z;
        if (axis == quader_geometry::QAxis::X) {
          axis_index = 0;
          component = value.x;
        } else if (axis == quader_geometry::QAxis::Y) {
          axis_index = 1;
          component = value.y;
        }
        return { axis_index, component < 0.0 ? -1 : 1 };
}

AxisTarget current_view_axis_target(const quader_poly::Transform3& camera_transform, quader::QVec3 pivot)
{
	const quader::QVec3 offset = camera_transform.origin - pivot;
        if (geometry_length_squared(offset) >
            kNavigationMinCameraDistance * kNavigationMinCameraDistance) {
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
  static constexpr std::array<AxisTarget, 4> kHorizontalRing{
      AxisTarget{2, 1},
      AxisTarget{0, 1},
      AxisTarget{2, -1},
      AxisTarget{0, -1},
  };
  static constexpr std::array<AxisTarget, 4> kVerticalRing{
      AxisTarget{2, 1},
      AxisTarget{1, 1},
      AxisTarget{2, -1},
      AxisTarget{1, -1},
  };

  if (direction == ViewAxisStepDirection::Left ||
      direction == ViewAxisStepDirection::Right) {
    const int step = direction == ViewAxisStepDirection::Right ? 1 : -1;
    const int ring_index = axis_target_ring_index(current, kHorizontalRing);
    if (ring_index >= 0) {
      return kHorizontalRing[(ring_index + step + 4) % 4];
    }
    return {0, step};
  }
  const int step = direction == ViewAxisStepDirection::Up ? 1 : -1;
  const int ring_index = axis_target_ring_index(current, kVerticalRing);
  if (ring_index >= 0) {
    return kVerticalRing[(ring_index + step + 4) % 4];
  }
	return { 1, step };
}

ViewAxis view_axis_from_index(int index)
{
	if (index == 0) {
          return ViewAxis::X;
        }
	if (index == 1) {
          return ViewAxis::Y;
        }
        return ViewAxis::Z;
}

TransformAxis transform_axis_from_index(int index)
{
	if (index == 0) {
          return TransformAxis::X;
        }
	if (index == 1) {
          return TransformAxis::Y;
        }
	if (index == 2) {
          return TransformAxis::Z;
        }
        return TransformAxis::None;
}


quader::QVec2 view_cube_center(quader::QVec2 viewport_size, quader::QVec2 margin)
{
	return {
		static_cast<float>(std::max(0.0, static_cast<double>(margin.x))),
		static_cast<float>(std::max(0.0, static_cast<double>(viewport_size.y) - static_cast<double>(margin.y))),
	};
}

quader::QVec2 project_view_cube_point(const quader_poly::Transform3& camera_transform, quader::QVec3 point, quader::QVec2 center, double scale)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);
	return {
		static_cast<float>(static_cast<double>(center.x) + geometry_dot(camera_basis.x_axis, point) * scale),
		static_cast<float>(static_cast<double>(center.y) - geometry_dot(camera_basis.y_axis, point) * scale),
	};
}

double view_cube_depth(const quader_poly::Transform3& camera_transform, quader::QVec3 point)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);
	return geometry_dot(camera_basis.z_axis, point);
}

std::vector<quader::QVec2> make_view_cube_face_polygon(
		const quader_poly::Transform3& camera_transform,
		quader::QVec2 center,
		double scale,
		const std::array<quader::QVec3, 4>& vertices)
{
	std::vector<quader::QVec2> polygon;
	polygon.reserve(vertices.size());
	for (quader::QVec3 vertex : vertices) {
		polygon.push_back(project_view_cube_point(camera_transform, vertex, center, scale));
	}
	return polygon;
}

quader::QVec2 polygon_center(const std::vector<quader::QVec2>& polygon)
{
	if (polygon.empty()) {
		return {};
	}
	double x = 0.0;
	double y = 0.0;
	for (quader::QVec2 point : polygon) {
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
	quader::QVec3 normal;
	std::array<quader::QVec3, 4> vertices;
};

const std::array<ViewCubeFaceDefinition, 6>& view_cube_face_definitions()
{
  static const std::array<ViewCubeFaceDefinition, 6> kDefinitions = {{
      {0,
       1,
       {1.0F, 0.0F, 0.0F},
       {quader::QVec3{1.0F, -1.0F, -1.0F}, quader::QVec3{1.0F, 1.0F, -1.0F},
        quader::QVec3{1.0F, 1.0F, 1.0F}, quader::QVec3{1.0F, -1.0F, 1.0F}}},
      {0,
       -1,
       {-1.0F, 0.0F, 0.0F},
       {quader::QVec3{-1.0F, -1.0F, 1.0F}, quader::QVec3{-1.0F, 1.0F, 1.0F},
        quader::QVec3{-1.0F, 1.0F, -1.0F}, quader::QVec3{-1.0F, -1.0F, -1.0F}}},
      {1,
       1,
       {0.0F, 1.0F, 0.0F},
       {quader::QVec3{-1.0F, 1.0F, -1.0F}, quader::QVec3{-1.0F, 1.0F, 1.0F},
        quader::QVec3{1.0F, 1.0F, 1.0F}, quader::QVec3{1.0F, 1.0F, -1.0F}}},
      {1,
       -1,
       {0.0F, -1.0F, 0.0F},
       {quader::QVec3{-1.0F, -1.0F, 1.0F}, quader::QVec3{-1.0F, -1.0F, -1.0F},
        quader::QVec3{1.0F, -1.0F, -1.0F}, quader::QVec3{1.0F, -1.0F, 1.0F}}},
      {2,
       1,
       {0.0F, 0.0F, 1.0F},
       {quader::QVec3{-1.0F, -1.0F, 1.0F}, quader::QVec3{1.0F, -1.0F, 1.0F},
        quader::QVec3{1.0F, 1.0F, 1.0F}, quader::QVec3{-1.0F, 1.0F, 1.0F}}},
      {2,
       -1,
       {0.0F, 0.0F, -1.0F},
       {quader::QVec3{1.0F, -1.0F, -1.0F}, quader::QVec3{-1.0F, -1.0F, -1.0F},
        quader::QVec3{-1.0F, 1.0F, -1.0F}, quader::QVec3{1.0F, 1.0F, -1.0F}}},
  }};
  return kDefinitions;
}

bool view_cube_same_corner(quader::QVec3 left, quader::QVec3 right)
{
	return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool view_cube_same_edge(quader::QVec3 left_start, quader::QVec3 left_end, quader::QVec3 right_start, quader::QVec3 right_end)
{
	return (view_cube_same_corner(left_start, right_start) && view_cube_same_corner(left_end, right_end)) ||
		(view_cube_same_corner(left_start, right_end) && view_cube_same_corner(left_end, right_start));
}

ViewAxis view_cube_edge_axis(quader::QVec3 start, quader::QVec3 end)
{
	if (std::abs(start.x - end.x) > 0.5F) {
          return ViewAxis::X;
        }
	if (std::abs(start.y - end.y) > 0.5F) {
          return ViewAxis::Y;
        }
        return ViewAxis::Z;
}

bool point_in_polygon(const std::vector<quader::QVec2>& polygon, quader::QVec2 point)
{
	std::vector<quader_geometry::QVec2d> geometry_polygon;
	geometry_polygon.reserve(polygon.size());
	for (quader::QVec2 vertex : polygon) {
		geometry_polygon.push_back(geometry_vec2(vertex));
	}
        return quader_geometry::point_in_or_on_polygon_2d(
            geometry_vec2(point),
            std::span<const quader_geometry::QVec2d>(geometry_polygon),
            kToolEpsilon);
}

std::vector<ViewCubeFacePrimitive> build_view_cube_faces_for_state(const quader_poly::Transform3& camera_transform, quader::QVec2 center, double scale)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);

	std::vector<ViewCubeFacePrimitive> faces;
	for (const ViewCubeFaceDefinition& definition : view_cube_face_definitions()) {
		const double normal_depth = geometry_dot(camera_basis.z_axis, definition.normal);
                if (normal_depth <= kViewCubeFaceVisibilityEpsilon) {
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

std::vector<ViewCubeEdgePrimitive> build_view_cube_edges_for_state(const quader_poly::Transform3& camera_transform, quader::QVec2 center, double scale)
{
	const quader_poly::Transform3 camera_basis = orthonormalized_basis(camera_transform);
	/**
	 * Stores the Edge Key data contract used by the modeling runtime domain.
	 */
	struct EdgeKey {
		quader::QVec3 start;
		quader::QVec3 end;
	};

	std::vector<EdgeKey> emitted_edges;
	std::vector<ViewCubeEdgePrimitive> edges;
	for (const ViewCubeFaceDefinition& definition : view_cube_face_definitions()) {
		const double normal_depth = geometry_dot(camera_basis.z_axis, definition.normal);
                if (normal_depth <= kViewCubeFaceVisibilityEpsilon) {
                  continue;
                }

                for (std::size_t index = 0; index < definition.vertices.size(); ++index) {
			const quader::QVec3 start = definition.vertices[index];
			const quader::QVec3 end = definition.vertices[(index + 1u) % definition.vertices.size()];
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

std::vector<ViewCubeEdgePrimitive> build_view_cube_axis_edges_for_state(const quader_poly::Transform3& camera_transform, quader::QVec2 center, double scale)
{
	const quader::QVec3 anchor { 1.0F, -1.0F, 1.0F };
	const struct AxisEdgeDefinition {
		ViewAxis axis;
		quader::QVec3 end;
        } axis_edges[3] = {
            {ViewAxis::X, {-1.0F, -1.0F, 1.0F}},
            {ViewAxis::Y, {1.0F, 1.0F, 1.0F}},
            {ViewAxis::Z, {1.0F, -1.0F, -1.0F}},
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

std::vector<ViewCubeAxisMarkerPrimitive> build_view_cube_markers_for_state(const quader_poly::Transform3& camera_transform, quader::QVec2 center, double radius)
{
	std::vector<ViewCubeAxisMarkerPrimitive> markers;
	const quader::QVec3 axes[3] = {
		{ 1.0F, 0.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
		{ 0.0F, 0.0F, 1.0F },
	};
	for (int axis_index = 0; axis_index < 3; ++axis_index) {
		quader::QVec2 projected = project_view_cube_point(camera_transform, axes[axis_index], {}, 1.0);
		const double depth = view_cube_depth(camera_transform, axes[axis_index]);
		if (vec2_is_tiny(projected)) {
			projected = depth >= 0.0 ? quader::QVec2 { 0.0F, -1.0F } : quader::QVec2 { 0.0F, 1.0F };
		}
		const quader::QVec2 normalized = vec2_normalize_or(projected, { 0.0F, -1.0F });
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

quader::QVec3 view_axis_vector(ViewAxis axis, int sign)
{
	const float value = sign < 0 ? -1.0F : 1.0F;
        if (axis == ViewAxis::X) {
          return {value, 0.0F, 0.0F};
        }
        if (axis == ViewAxis::Y) {
          return {0.0F, value, 0.0F};
        }
        return { 0.0F, 0.0F, value };
}

quader::QVec3 viewport_camera_forward(const quader_poly::Transform3& camera_transform)
{
	const quader::QVec3 forward = geometry_scale(camera_transform.z_axis, -1.0);
	return geometry_is_tiny(forward) ? quader::QVec3 { 0.0F, 0.0F, -1.0F } : geometry_normalized_axis(forward);
}

quader::QVec3 viewport_camera_right(const quader_poly::Transform3& camera_transform)
{
	return geometry_is_tiny(camera_transform.x_axis) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : geometry_normalized_axis(camera_transform.x_axis);
}

quader::QVec3 viewport_camera_up(const quader_poly::Transform3& camera_transform)
{
	return geometry_is_tiny(camera_transform.y_axis) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : geometry_normalized_axis(camera_transform.y_axis);
}

double viewport_distance_to_pivot(const quader_poly::Transform3& camera_transform, quader::QVec3 pivot)
{
	const quader::QVec3 to_pivot = pivot - camera_transform.origin;
	const double forward_distance = geometry_dot(to_pivot, viewport_camera_forward(camera_transform));
        if (forward_distance > kNavigationMinCameraDistance) {
          return forward_distance;
        }
        return std::max(kNavigationMinCameraDistance,
                        geometry_length(to_pivot));
}

double viewport_aspect_for_size(quader::QVec2 viewport_size)
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

double viewport_orthographic_size_for_perspective(const quader_poly::Transform3& camera_transform, quader::QVec3 pivot, double fov_degrees)
{
	const double distance = std::max(0.1, geometry_length(camera_transform.origin - pivot));
        return std::max(
            kNavigationMinOrthographicSize,
            2.0 * distance *
                std::tan(viewport_vertical_fov_radians(fov_degrees) * 0.5));
}

double viewport_orbit_sensitivity(double mouse_sensitivity)
{
  return kNavigationOrbitRadiansPerPixel * std::max(0.1, mouse_sensitivity);
}

double viewport_fly_look_sensitivity(double mouse_sensitivity)
{
  return kNavigationFlyLookRadiansPerPixel * std::max(0.1, mouse_sensitivity);
}

double viewport_fly_move_distance(double delta_seconds, bool fast, bool slow)
{
  double speed = kNavigationFlyBaseSpeed;
  if (fast) {
    speed *= kNavigationFlyFastMultiplier;
  }
	if (slow) {
          speed *= kNavigationFlySlowMultiplier;
        }
        const double clamped_delta =
            std::clamp(delta_seconds, 0.0, kNavigationFlyMaxDeltaSeconds);
        return speed * clamped_delta;
}

void reset_viewport_navigation(ViewportNavigationState& state)
{
	state = ViewportNavigationState();
}

void begin_viewport_navigation(ViewportNavigationState& state, bool pan)
{
  state.mode =
      pan ? ViewportNavigationMode::Pan : ViewportNavigationMode::Orbit;
  state.fly_camera_id = 0;
  state.fly_pane_id.clear();
  clear_viewport_orbit_snap_source(state);
  ensure_viewport_navigation_pivot(state);
}

void begin_viewport_fly_navigation(ViewportNavigationState& state, std::uint64_t camera_id, std::string_view pane_id)
{
  state.mode = ViewportNavigationMode::Fly;
  state.fly_camera_id = camera_id;
  state.fly_pane_id = std::string(pane_id);
  clear_viewport_orbit_snap_source(state);
}

void end_viewport_navigation(ViewportNavigationState& state)
{
  state.mode = ViewportNavigationMode::None;
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
  return state.mode == ViewportNavigationMode::Pan;
}

bool viewport_navigation_is_orbit(const ViewportNavigationState& state)
{
  return state.mode == ViewportNavigationMode::Orbit;
}

bool viewport_navigation_is_fly(const ViewportNavigationState& state)
{
  return state.mode == ViewportNavigationMode::Fly;
}

bool viewport_navigation_is_fly_camera(const ViewportNavigationState& state, std::uint64_t camera_id)
{
  return state.mode == ViewportNavigationMode::Fly &&
         state.fly_camera_id != 0 && state.fly_camera_id == camera_id;
}

bool viewport_navigation_is_active(const ViewportNavigationState& state, bool view_cube_dragging)
{
  return state.mode != ViewportNavigationMode::None || view_cube_dragging;
}

ViewportNavigationResult viewport_orbit(const ViewportOrbitInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_orbit");
	const double sensitivity = std::max(0.00001, input.sensitivity);
	const double yaw = -static_cast<double>(input.screen_delta.x) * sensitivity;
	const double pitch = -static_cast<double>(input.screen_delta.y) * sensitivity;

	const quader_poly::Transform3 yaw_rotation = rotation_basis({ 0.0F, 1.0F, 0.0F }, yaw);
	const quader::QVec3 right = geometry_normalized_axis(geometry_transform_vector(yaw_rotation, viewport_camera_right(input.camera_transform)));
	const quader_poly::Transform3 pitch_rotation = rotation_basis(geometry_is_tiny(right) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : right, pitch);
	const quader_poly::Transform3 rotation = clamped_rotation_to_upright(input.camera_transform, basis_multiply(pitch_rotation, yaw_rotation));

	quader_poly::Transform3 unsnapped_result = input.camera_transform;
	const quader::QVec3 rotated_offset = geometry_transform_vector(rotation, input.camera_transform.origin - input.pivot);
	unsnapped_result.origin = input.pivot + rotated_offset;
	const quader_poly::Transform3 rotated_basis = rotated_camera_basis(input.camera_transform, rotation);
	unsnapped_result.x_axis = rotated_basis.x_axis;
	unsnapped_result.y_axis = rotated_basis.y_axis;
	unsnapped_result.z_axis = rotated_basis.z_axis;

	quader_poly::Transform3 visible_result = unsnapped_result;
	const ViewportAxisSnapTransformResult snap =
		input.snap_to_orthogonal ? axis_snap_transform(unsnapped_result, input.pivot, visible_result) : ViewportAxisSnapTransformResult {};
	ViewportNavigationResult result = navigation_result(visible_result, input.pivot);
	result.orthogonal_snap = snap.orthographic_axis;
	result.unsnapped_camera_transform = unsnapped_result;
	return result;
}

ViewportNavigationResult viewport_pan(const ViewportPanInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_pan");
	const double viewport_height = std::max(1.0, static_cast<double>(input.camera.viewport_size.y));
	double world_per_pixel = 0.01;
        if (input.camera.projection == CameraProjection::Orthographic) {
          world_per_pixel = std::max(0.0001, input.camera.orthographic_size) /
                            viewport_height;
        } else {
          const double distance = std::max(
              kNavigationMinCameraDistance,
              viewport_distance_to_pivot(input.camera.transform, input.pivot));
          const double vertical_span =
              2.0 * distance *
              std::tan(viewport_vertical_fov_radians(input.camera.fov_degrees) *
                       0.5);
          world_per_pixel = std::max(0.0001, vertical_span) / viewport_height;
        }

        const quader::QVec3 move =
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

        const double factor = std::pow(kNavigationZoomStepFactor, input.steps);
        if (input.camera.projection == CameraProjection::Orthographic) {
          const double new_size = capped_orthographic_zoom_size(
              input.camera.orthographic_size, factor, input.steps);
          return navigation_result(input.camera.transform, input.pivot,
                                   new_size);
        }

        const quader::QVec3 forward = viewport_camera_forward(input.camera.transform);
        const double distance = std::max(
            kNavigationMinCameraDistance,
            viewport_distance_to_pivot(input.camera.transform, input.pivot));
        const double new_distance =
            std::max(kNavigationMinCameraDistance, distance / factor);
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

        const double factor = std::pow(kNavigationZoomStepFactor, input.steps);
        const quader::QVec3 ray_direction = geometry_is_tiny(input.ray_direction) ? viewport_camera_forward(input.camera.transform) : geometry_normalized_axis(input.ray_direction);
        const double fallback_distance = std::max(
            kNavigationMinCameraDistance,
            viewport_distance_to_pivot(input.camera.transform, input.pivot));
        quader::QVec3 cursor_target = input.has_cursor_target ? input.cursor_target : input.ray_origin + geometry_scale(ray_direction, fallback_distance);

        if (input.camera.projection == CameraProjection::Orthographic) {
          const double current_size = std::max(kNavigationMinOrthographicSize,
                                               input.camera.orthographic_size);
          const double new_size =
              capped_orthographic_zoom_size(current_size, factor, input.steps);
          quader::QVec3 cursor_offset =
              input.ray_origin - input.camera.transform.origin;
          cursor_offset =
              cursor_offset -
              geometry_scale(ray_direction,
                             geometry_dot(cursor_offset, ray_direction));
          quader_poly::Transform3 result_transform = input.camera.transform;
          result_transform.origin +=
              geometry_scale(cursor_offset, 1.0 - (new_size / current_size));
          return navigation_result(result_transform, cursor_target, new_size);
        }

        double target_distance = geometry_dot(cursor_target - input.ray_origin, ray_direction);
        if (target_distance <= kNavigationMinCameraDistance) {
          target_distance = fallback_distance;
          cursor_target =
              input.ray_origin + geometry_scale(ray_direction, target_distance);
        }
        const double new_distance =
            std::max(kNavigationMinCameraDistance, target_distance / factor);
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
	const quader::QVec3 right = geometry_normalized_axis(geometry_transform_vector(yaw_rotation, viewport_camera_right(input.camera_transform)));
	const quader_poly::Transform3 pitch_rotation = rotation_basis(geometry_is_tiny(right) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : right, pitch);
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
	quader::QVec3 world_direction =
			geometry_scale(viewport_camera_right(input.camera_transform), input.local_direction.x) +
			geometry_scale(viewport_camera_up(input.camera_transform), input.local_direction.y) +
			geometry_scale(viewport_camera_forward(input.camera_transform), input.local_direction.z);
	if (geometry_is_tiny(world_direction)) {
		return navigation_result(input.camera_transform, input.pivot);
	}
	const quader::QVec3 move = geometry_scale(geometry_normalized_axis(world_direction), input.distance);
	quader_poly::Transform3 result_transform = input.camera_transform;
	result_transform.origin += move;
	return navigation_result(result_transform, input.pivot + move);
}

ViewportNavigationResult viewport_frame_bounds(const ViewportFrameBoundsInput& input)
{
	QDR_PROFILE_SCOPE("qdr_editor_core.viewport_frame_bounds");
	const quader::QVec3 center = geometry_scale(input.min_bounds + input.max_bounds, 0.5);
	const quader::QVec3 extents = geometry_scale(input.max_bounds - input.min_bounds, 0.5);
	double radius = geometry_length(extents);
	if (radius <= 0.0001) {
		radius = 1.0;
	}

	const double margin = std::max(1.0, input.margin);
	quader_poly::Transform3 result_transform = input.camera.transform;
        if (input.camera.projection == CameraProjection::Orthographic) {
          const double aspect =
              viewport_aspect_for_size(input.camera.viewport_size);
          const double needed_height =
              std::max(static_cast<double>(extents.y) * 2.0,
                       (static_cast<double>(extents.x) * 2.0) / aspect);
          const double size = std::clamp(
              std::max(needed_height, radius * 2.0) * margin,
              kNavigationMinOrthographicSize, kNavigationMaxOrthographicSize);
          result_transform.origin =
              center -
              geometry_scale(
                  viewport_camera_forward(input.camera.transform),
                  std::max(kNavigationDefaultFrameDistance, radius * 2.0));
          return navigation_result(result_transform, center, size);
        }

        const double aspect = viewport_aspect_for_size(input.camera.viewport_size);
	const double vertical_fov = viewport_vertical_fov_radians(input.camera.fov_degrees);
	const double horizontal_fov = 2.0 * std::atan(std::tan(vertical_fov * 0.5) * aspect);
	const double fitting_fov = std::max(0.01, std::min(vertical_fov, horizontal_fov));
        const double distance =
            std::max(kNavigationMinCameraDistance,
                     (radius * margin) / std::sin(fitting_fov * 0.5));
        result_transform.origin = center - geometry_scale(viewport_camera_forward(input.camera.transform), distance);
	return navigation_result(result_transform, center);
}

ViewportNavigationResult viewport_view_axis(const ViewportCameraState& camera, quader::QVec3 pivot, ViewAxis axis, int sign)
{
	const quader::QVec3 offset_axis = view_axis_vector(axis, sign);
	double distance = viewport_distance_to_pivot(camera.transform, pivot);
        if (distance <= kNavigationMinCameraDistance) {
          distance = kNavigationDefaultFrameDistance;
        }

        double camera_size = kNavigationDefaultFrameDistance;
        if (camera.projection == CameraProjection::Orthographic) {
          camera_size = std::clamp(camera.orthographic_size,
                                   kNavigationMinOrthographicSize,
                                   kNavigationMaxOrthographicSize);
        } else {
          const double visible_height =
              2.0 * distance *
              std::tan(viewport_vertical_fov_radians(camera.fov_degrees) * 0.5);
          camera_size =
              std::clamp(visible_height, kNavigationMinOrthographicSize,
                         kNavigationMaxOrthographicSize);
        }

        quader_poly::Transform3 result_transform = camera.transform;
	result_transform.origin = pivot + geometry_scale(offset_axis, distance);
	const quader_poly::Transform3 basis = orthogonal_camera_basis_for_offset_axis(offset_axis);
	result_transform.x_axis = basis.x_axis;
	result_transform.y_axis = basis.y_axis;
	result_transform.z_axis = basis.z_axis;
	return navigation_result(result_transform, pivot, camera_size);
}

ViewportNavigationResult viewport_step_view_axis(const ViewportCameraState& camera, quader::QVec3 pivot, ViewAxisStepDirection direction)
{
	const AxisTarget current = current_view_axis_target(camera.transform, pivot);
	const AxisTarget target = stepped_axis_target(current, direction);
	return viewport_view_axis(camera, pivot, view_axis_from_index(target.axis_index), target.sign);
}

ViewCubePrimitives build_view_cube_primitives(const ViewCubePrimitiveInput& input)
{
  const double widget_size =
      std::clamp(input.widget_size, kViewCubeMinSize, kViewCubeMaxSize);
  const quader::QVec2 center =
      view_cube_center(input.viewport_size, input.margin);
  const double cube_scale = widget_size * kViewCubeHalfFactor;
  const double marker_radius = widget_size * kViewCubeAxisFactor;

  ViewCubePrimitives primitives;
  primitives.ok = true;
  primitives.screen_position = center;
  primitives.rect_position = {
      static_cast<float>(static_cast<double>(center.x) - widget_size * 0.5),
      static_cast<float>(static_cast<double>(center.y) - widget_size * 0.5),
  };
  primitives.rect_size = {static_cast<float>(widget_size),
                          static_cast<float>(widget_size)};
  primitives.faces = build_view_cube_faces_for_state(input.camera_transform,
                                                     center, cube_scale);
  primitives.edges = build_view_cube_edges_for_state(input.camera_transform,
                                                     center, cube_scale);
  primitives.axis_edges = build_view_cube_axis_edges_for_state(
      input.camera_transform, center, cube_scale);
  primitives.axis_markers = build_view_cube_markers_for_state(
      input.camera_transform, center, marker_radius);
  return primitives;
}

ViewCubeHit pick_view_cube(const ViewCubePickInput& input)
{
  const double widget_size = std::clamp(input.primitives.widget_size,
                                        kViewCubeMinSize, kViewCubeMaxSize);
  const quader::QVec2 center =
      view_cube_center(input.primitives.viewport_size, input.primitives.margin);
  const double radius = widget_size * 0.5;
  const quader::QVec2 from_center = vec2_sub(input.screen_position, center);

  ViewCubeHit hit;
  hit.ok = true;
  hit.hit = false;
  if (vec2_dot(from_center, from_center) > radius * radius) {
    return hit;
  }

	int best_face_index = -1;
	double best_depth = -1000000.0;
        const double cube_scale = widget_size * kViewCubeHalfFactor;
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
                hit.kind = ViewCubeHitKind::Face;
                hit.axis = face.axis;
		hit.sign = face.sign;
		hit.depth = face.depth;
		hit.screen_position = face.screen_position;
		return hit;
	}

        hit.kind = ViewCubeHitKind::Body;
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

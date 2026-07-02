////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/transform_gizmo.hpp>
#include <editor/interactions/tool_drag_math.hpp>
#include <editor/interactions/tool_transform_math.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>
#include <diagnostics/profile.hpp>
#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace quader_editor_core {

namespace {

constexpr double kGizmoCircleSize = 1.1;
constexpr double kGizmoPlaneSize = 0.2;
constexpr double kGizmoPlaneDistance = 0.3;
constexpr double kGizmoArrowSize = 0.35;
constexpr double kGizmoArrowOffset = kGizmoCircleSize + 0.3;
constexpr double kGizmoArrowTip = kGizmoArrowOffset + kGizmoArrowSize;
constexpr double kGizmoScaleOffset = kGizmoCircleSize + 0.3;
constexpr double kGizmoScaleTip = kGizmoScaleOffset * 1.11;
constexpr double kGizmoViewRotationScale = 1.14;

quader_geometry::QVec2d tool_geometry_vec2(quader::QVec2 value)
{
	return { value.x, value.y };
}

quader_geometry::QVec3f tool_geometry_vec3(quader::QVec3 value)
{
	return { value.x, value.y, value.z };
}

quader::QVec3 tool_poly_vec3(quader_geometry::QVec3f value)
{
	return { value.x, value.y, value.z };
}

double vec2_distance_squared(quader::QVec2 left, quader::QVec2 right)
{
	return quader_geometry::length_squared(tool_geometry_vec2(left) - tool_geometry_vec2(right));
}

bool vec3_is_tiny(quader::QVec3 value, double epsilon = kToolEpsilon) {
  return quader_geometry::length_squared(tool_geometry_vec3(value)) <=
         static_cast<float>(epsilon * epsilon);
}

quader::QVec3 vec3_scale(quader::QVec3 value, double scalar)
{
	return tool_poly_vec3(tool_geometry_vec3(value) * static_cast<float>(scalar));
}

double vec3_dot(quader::QVec3 left, quader::QVec3 right)
{
	return quader_geometry::dot(tool_geometry_vec3(left), tool_geometry_vec3(right));
}

quader::QVec3 normalized_axis(quader::QVec3 value)
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
          basis.axis = TransformAxis::None;
          return basis;
	}
        if (axis == TransformAxis::X) {
          basis.ring_world_a = {0.0F, 1.0F, 0.0F};
          basis.ring_world_b = {0.0F, 0.0F, 1.0F};
        } else if (axis == TransformAxis::Y) {
          basis.ring_world_a = {1.0F, 0.0F, 0.0F};
          basis.ring_world_b = {0.0F, 0.0F, 1.0F};
        } else {
          basis.ring_world_a = {1.0F, 0.0F, 0.0F};
          basis.ring_world_b = {0.0F, 1.0F, 0.0F};
        }
        basis.world_direction = normalized_axis(transform_vector(global_transform, basis.local_direction));
	basis.ring_world_a = normalized_axis(transform_vector(global_transform, basis.ring_world_a));
	basis.ring_world_b = normalized_axis(transform_vector(global_transform, basis.ring_world_b));
	if (vec3_is_tiny(basis.world_direction)) {
		basis.world_direction = basis.local_direction;
	}
	return basis;
}

double transform_gizmo_axis_visibility_score(quader::QVec3 camera_origin, quader::QVec3 world_center, quader::QVec3 world_axis)
{
	if (vec3_is_tiny(world_axis)) {
		return 0.0;
	}
	const quader::QVec3 axis = normalized_axis(world_axis);
	const quader::QVec3 view_direction = camera_origin - world_center;
	if (vec3_is_tiny(view_direction)) {
		return 1.0;
	}
	return std::clamp(1.0 - std::abs(vec3_dot(axis, normalized_axis(view_direction))), 0.0, 1.0);
}

double transform_gizmo_axis_projection_scale(quader::QVec3 camera_origin, quader::QVec3 world_center, quader::QVec3 world_axis)
{
	if (vec3_is_tiny(world_axis)) {
		return 0.0;
	}
	const quader::QVec3 axis = normalized_axis(world_axis);
	const quader::QVec3 view_direction = camera_origin - world_center;
	if (vec3_is_tiny(view_direction)) {
		return 1.0;
	}
	const double axis_dot_view = std::clamp(std::abs(vec3_dot(axis, normalized_axis(view_direction))), 0.0, 1.0);
	return std::sqrt(std::max(0.0, 1.0 - axis_dot_view * axis_dot_view));
}

bool transform_gizmo_axis_is_visible(quader::QVec3 camera_origin, quader::QVec3 world_center, quader::QVec3 world_axis)
{
  return transform_gizmo_axis_visibility_score(camera_origin, world_center,
                                               world_axis) >=
         kTransformGizmoAxisHideScore;
}

double transform_gizmo_axis_alpha(quader::QVec3 camera_origin, quader::QVec3 world_center, quader::QVec3 world_axis)
{
	const double score = transform_gizmo_axis_visibility_score(camera_origin, world_center, world_axis);
        if (score <= kTransformGizmoAxisHideScore) {
          return 0.0;
        }
        if (score >= kTransformGizmoAxisFullScore) {
          return 1.0;
        }
        return (score - kTransformGizmoAxisHideScore) /
               (kTransformGizmoAxisFullScore - kTransformGizmoAxisHideScore);
}

quader::QVec3 transform_gizmo_ring_point(quader::QVec3 world_center, const TransformGizmoAxisBasis& axis, double radius, double angle)
{
	return world_center + vec3_scale(axis.ring_world_a * static_cast<float>(std::cos(angle)) + axis.ring_world_b * static_cast<float>(std::sin(angle)), radius);
}

namespace {

quader_geometry::QVec2d geometry_vec2(quader::QVec2 value)
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
  return tool == ViewportInteractionTool::Move ||
         tool == ViewportInteractionTool::Rotate ||
         tool == ViewportInteractionTool::Scale;
}

double viewport_world_units_per_pixel_impl(const ViewportCameraState& camera, quader::QVec3 anchor)
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

void add_gizmo_axis(TransformGizmoFrame& frame, const ViewportCameraState& camera, TransformAxis axis, quader::QVec3 center, double axis_length)
{
	const quader::QVec3 direction = axis_vector(axis);
	if (vec3_is_tiny(direction)) {
		return;
	}
	const quader::QVec3 tip = center + vec3_scale(direction, axis_length);
	quader::QVec2 screen_start;
	quader::QVec2 screen_tip;
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
		quader::QVec3 center,
		quader::QVec3 axis_a,
		quader::QVec3 axis_b,
		double offset,
		double half_size)
{
	const quader::QVec3 a = vec3_is_tiny(axis_a) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : normalized_axis(axis_a);
	const quader::QVec3 b = vec3_is_tiny(axis_b) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(axis_b);
	const quader::QVec3 handle_center = center + vec3_scale(a + b, offset);
	const std::array<quader::QVec3, 4> world {
		handle_center - vec3_scale(a, half_size) - vec3_scale(b, half_size),
		handle_center + vec3_scale(a, half_size) - vec3_scale(b, half_size),
		handle_center + vec3_scale(a, half_size) + vec3_scale(b, half_size),
		handle_center - vec3_scale(a, half_size) + vec3_scale(b, half_size),
	};
	std::array<quader::QVec2, 4> screen {};
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

double screen_sized_world_radius(const ViewportCameraState& camera, quader::QVec3 center, double screen_size)
{
	const double pixel_world = viewport_world_units_per_pixel_impl(camera, center);
	return std::max(0.001, pixel_world * std::max(24.0, screen_size));
}

void add_gizmo_ring(TransformGizmoFrame& frame, const ViewportCameraState& camera, TransformAxis axis, quader::QVec3 center, double screen_size, int ring_segment_count)
{
	const TransformGizmoAxisBasis basis = transform_gizmo_axis_basis(axis);
        if (basis.axis == TransformAxis::None) {
          return;
        }
        const double radius = screen_sized_world_radius(camera, center, screen_size);
	if (radius <= 0.0001) {
		return;
	}
	const int segment_count = std::max(3, ring_segment_count);
	const quader::QVec3 view_direction = normalized_axis(camera.transform.origin - center);
	for (int index = 0; index < segment_count; ++index) {
          const double a0 = kTransformGizmoTau * static_cast<double>(index) /
                            static_cast<double>(segment_count);
          const double a1 = kTransformGizmoTau *
                            static_cast<double>(index + 1) /
                            static_cast<double>(segment_count);
          const double mid_angle = (a0 + a1) * 0.5;
          const quader::QVec3 p0 =
              transform_gizmo_ring_point(center, basis, radius, a0);
          const quader::QVec3 p1 =
              transform_gizmo_ring_point(center, basis, radius, a1);
          const quader::QVec3 pm =
              transform_gizmo_ring_point(center, basis, radius, mid_angle);
          quader::QVec2 screen0;
          quader::QVec2 screen1;
          if (!project_viewport_point(camera, p0, screen0) ||
              !project_viewport_point(camera, p1, screen1)) {
            continue;
          }
		TransformGizmoRingSegmentPrimitive segment;
		segment.axis = axis;
		segment.world_start = p0;
		segment.world_end = p1;
		segment.screen_start = screen0;
		segment.screen_end = screen1;
		segment.front_facing = vec3_is_tiny(view_direction) || vec3_dot(normalized_axis(pm - center), view_direction) >= -0.01;
		segment.alpha = segment.front_facing ? 1.0F : 0.28F;
		frame.ring_segments.push_back(segment);
	}
}

void add_gizmo_trackball_ring(TransformGizmoFrame& frame, const ViewportCameraState& camera, quader::QVec3 center, double screen_size, int ring_segment_count)
{
  const double radius = screen_sized_world_radius(
      camera, center, screen_size * kGizmoViewRotationScale);
  quader::QVec3 right = normalized_axis(camera.transform.x_axis);
  quader::QVec3 up = normalized_axis(camera.transform.y_axis);
  if (vec3_is_tiny(right)) {
    right = {1.0F, 0.0F, 0.0F};
  }
	up = normalized_axis(up - vec3_scale(right, vec3_dot(up, right)));
	if (vec3_is_tiny(up)) {
		up = { 0.0F, 1.0F, 0.0F };
	}
	const int segment_count = std::max(16, ring_segment_count);
	for (int index = 0; index < segment_count; ++index) {
          const double a0 = kTransformGizmoTau * static_cast<double>(index) /
                            static_cast<double>(segment_count);
          const double a1 = kTransformGizmoTau *
                            static_cast<double>(index + 1) /
                            static_cast<double>(segment_count);
          const quader::QVec3 p0 =
              center + vec3_scale(right * static_cast<float>(std::cos(a0)) +
                                      up * static_cast<float>(std::sin(a0)),
                                  radius);
          const quader::QVec3 p1 =
              center + vec3_scale(right * static_cast<float>(std::cos(a1)) +
                                      up * static_cast<float>(std::sin(a1)),
                                  radius);
          quader::QVec2 screen0;
          quader::QVec2 screen1;
          if (!project_viewport_point(camera, p0, screen0) ||
              !project_viewport_point(camera, p1, screen1)) {
            continue;
          }
		TransformGizmoTrackballSegmentPrimitive segment;
		segment.world_start = p0;
		segment.world_end = p1;
		segment.screen_start = screen0;
		segment.screen_end = screen1;
		frame.trackball_segments.push_back(segment);
	}
}

bool point_in_convex_gizmo_polygon(quader::QVec2 point, const std::array<quader::QVec2, 4>& polygon)
{
	std::array<quader_geometry::QVec2d, 4> geometry_polygon {};
	for (std::size_t index = 0; index < polygon.size(); ++index) {
		geometry_polygon[index] = geometry_vec2(polygon[index]);
	}
	const double signed_area = quader_geometry::polygon_signed_area(
			std::span<const quader_geometry::QVec2d>(geometry_polygon.data(), geometry_polygon.size())) * 2.0;
	if (std::abs(signed_area) <= 0.001) {
		return false;
	}

	return quader_geometry::point_in_or_on_polygon_2d(
		geometry_vec2(point),
		std::span<const quader_geometry::QVec2d>(geometry_polygon.data(), geometry_polygon.size()),
		0.001);
}

quader::QVec2 plane_centroid(const TransformGizmoPlanePrimitive& plane)
{
	return {
		(plane.screen0.x + plane.screen1.x + plane.screen2.x + plane.screen3.x) * 0.25F,
		(plane.screen0.y + plane.screen1.y + plane.screen2.y + plane.screen3.y) * 0.25F,
	};
}

} // namespace

double viewport_world_units_per_pixel(const ViewportCameraState& camera, quader::QVec3 anchor)
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

	quader::QVec2 screen_pivot;
	if (!project_viewport_point(input.camera, input.pivot, screen_pivot)) {
		return frame;
	}

	const double visual_scale = clamped_gizmo_scale(input.gizmo_scale);
	const double unit_screen_size = std::max(1.0, input.gizmo_size_pixels) * visual_scale;
	frame.info.ok = true;
	frame.info.pivot = input.pivot;
	frame.info.screen_pivot = screen_pivot;

        if (input.tool == ViewportInteractionTool::Rotate) {
          const double ring_screen_size = unit_screen_size * kGizmoCircleSize;
          add_gizmo_trackball_ring(frame, input.camera, input.pivot,
                                   ring_screen_size, input.ring_segments);
          add_gizmo_ring(frame, input.camera, TransformAxis::X, input.pivot,
                         ring_screen_size, input.ring_segments);
          add_gizmo_ring(frame, input.camera, TransformAxis::Y, input.pivot,
                         ring_screen_size, input.ring_segments);
          add_gizmo_ring(frame, input.camera, TransformAxis::Z, input.pivot,
                         ring_screen_size, input.ring_segments);
          return frame;
        }

        const double pixel_world = viewport_world_units_per_pixel(input.camera, input.pivot);
        const double axis_length =
            pixel_world * unit_screen_size *
            (input.tool == ViewportInteractionTool::Scale ? kGizmoScaleTip
                                                          : kGizmoArrowTip);
        const double plane_offset =
            pixel_world * unit_screen_size *
            (kGizmoPlaneDistance + kGizmoPlaneSize * 0.5);
        const double plane_half_size =
            pixel_world * unit_screen_size * (kGizmoPlaneSize * 0.5);
        add_gizmo_axis(frame, input.camera, TransformAxis::X, input.pivot,
                       axis_length);
        add_gizmo_axis(frame, input.camera, TransformAxis::Y, input.pivot,
                       axis_length);
        add_gizmo_axis(frame, input.camera, TransformAxis::Z, input.pivot,
                       axis_length);
        add_gizmo_plane(frame, input.camera, TransformAxis::Xy, input.pivot,
                        {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, plane_offset,
                        plane_half_size);
        add_gizmo_plane(frame, input.camera, TransformAxis::Xz, input.pivot,
                        {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, plane_offset,
                        plane_half_size);
        add_gizmo_plane(frame, input.camera, TransformAxis::Yz, input.pivot,
                        {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, plane_offset,
                        plane_half_size);
        return frame;
}

TransformGizmoPickHit pick_transform_gizmo(const TransformGizmoFrameInput& input, quader::QVec2 screen_position)
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

        if (input.tool == ViewportInteractionTool::Move ||
            input.tool == ViewportInteractionTool::Scale) {
          std::vector<TransformGizmoPickCandidate> candidates;
          candidates.reserve(frame.axes.size());
          for (const TransformGizmoAxisPrimitive &axis : frame.axes) {
            TransformGizmoPickCandidate candidate;
            candidate.axis = axis.axis;
            candidate.local_direction = axis_vector(axis.axis);
            candidate.world_direction = candidate.local_direction;
            candidate.screen_tip = axis.screen_tip;
            candidate.visible = true;
            candidates.push_back(candidate);
          }
          const TransformGizmoPickHit axis_hit =
              input.tool == ViewportInteractionTool::Scale
                  ? pick_scale_gizmo_axis(
                        screen_position, frame.info.screen_pivot, candidates,
                        pick_radius,
                        std::max(1.0, input.gizmo_size_pixels) *
                            clamped_gizmo_scale(frame.info.gizmo_scale))
                  : pick_move_gizmo_axis(screen_position,
                                         frame.info.screen_pivot, candidates,
                                         pick_radius);
          if (axis_hit.hit) {
            return axis_hit;
          }
          for (const TransformGizmoPlanePrimitive &plane : frame.planes) {
            const std::array<quader::QVec2, 4> polygon{
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
          return result;
        }

        if (input.tool == ViewportInteractionTool::Rotate) {
          std::vector<TransformGizmoRingPickCandidate> candidates;
          candidates.reserve(frame.ring_segments.size());
          for (const TransformGizmoRingSegmentPrimitive &segment :
               frame.ring_segments) {
            TransformGizmoRingPickCandidate candidate;
            candidate.axis = segment.axis;
            candidate.local_direction = rotation_axis_vector(segment.axis);
            candidate.world_direction = candidate.local_direction;
            candidate.a = segment.screen_start;
            candidate.b = segment.screen_end;
            candidates.push_back(candidate);
          }
          return pick_rotate_gizmo_axis(screen_position, candidates,
                                        pick_radius);
        }
        return result;
}

double distance_to_screen_segment_squared(quader::QVec2 point, quader::QVec2 a, quader::QVec2 b)
{
	const quader_geometry::QVec2d geometry_point = geometry_vec2(point);
	const quader_geometry::QVec2d geometry_a = geometry_vec2(a);
	const quader_geometry::QVec2d geometry_b = geometry_vec2(b);
        const quader_geometry::QClosestSegmentPoint2<double> closest =
            quader_geometry::closest_point_on_segment<double>(
                geometry_point, geometry_a, geometry_b,
                std::sqrt(kScreenEpsilon));
        if (!closest.valid || quader_geometry::length_squared(
                                  geometry_b - geometry_a) <= kScreenEpsilon) {
          return vec2_distance_squared(point, a);
        }
        return vec2_distance_squared(point, { static_cast<float>(closest.point.x), static_cast<float>(closest.point.y) });
}

TransformGizmoPickHit pick_move_gizmo_axis(quader::QVec2 screen_position, quader::QVec2 screen_center, const std::vector<TransformGizmoPickCandidate>& candidates, double pick_radius)
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

TransformGizmoPickHit pick_scale_gizmo_axis(quader::QVec2 screen_position, quader::QVec2 screen_pivot, const std::vector<TransformGizmoPickCandidate>& candidates, double pick_radius, double screen_size)
{
	TransformGizmoPickHit hit;
	const double radius = std::max(1.0, pick_radius);
	const double center_safe_radius = std::max(radius, std::min(std::max(24.0, screen_size) * 0.35, std::max(radius * 1.75, 12.0)));
	const double center_distance_squared = vec2_distance_squared(screen_position, screen_pivot);
	if (center_distance_squared <= center_safe_radius * center_safe_radius) {
		hit.hit = true;
                hit.axis = TransformAxis::All;
                hit.local_direction = { 1.0F, 1.0F, 1.0F };
		hit.world_direction = { 1.0F, 1.0F, 1.0F };
		hit.screen_tip = { static_cast<float>(static_cast<double>(screen_pivot.x) + std::max(24.0, screen_size)), screen_pivot.y };
		hit.distance = std::sqrt(center_distance_squared);
		return hit;
	}
	return pick_move_gizmo_axis(screen_position, screen_pivot, candidates, radius);
}

TransformGizmoPickHit pick_rotate_gizmo_axis(quader::QVec2 screen_position, const std::vector<TransformGizmoRingPickCandidate>& candidates, double pick_radius)
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

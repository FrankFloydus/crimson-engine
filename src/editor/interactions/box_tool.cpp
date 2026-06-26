////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/box_tool.h>
#include <editor/modeling/runtime/editor_tool.h>
#include <editor/interactions/tool_drag_math.h>
#include "tool_math_internal.h"

#include <mesh/geometry/geometry.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace quader_editor_core {
namespace {

quader_geometry::QVec3d to_box_geometry_vec3(quader::QVec3 value)
{
	return { value.x, value.y, value.z };
}

quader::QVec3 to_box_poly_vec3(quader_geometry::QVec3d value)
{
	return { static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z) };
}

Bounds3 to_box_editor_bounds(const quader_geometry::QAabb3<double>& bounds)
{
	Bounds3 result;
	if (!quader_geometry::aabb_is_valid(bounds)) {
		return result;
	}
	result.empty = false;
	result.min = to_box_poly_vec3(bounds.min);
	result.max = to_box_poly_vec3(bounds.max);
	return result;
}

quader_geometry::QRay3<double> to_box_geometry_ray(quader_poly::Ray ray)
{
	return { to_box_geometry_vec3(ray.origin), to_box_geometry_vec3(ray.direction) };
}

quader_geometry::QLine3<double> to_box_geometry_line(quader_poly::Ray ray)
{
	return { to_box_geometry_vec3(ray.origin), to_box_geometry_vec3(ray.direction) };
}

quader_geometry::QPlane3<double> to_box_geometry_plane(quader::QVec3 origin, quader::QVec3 normal)
{
	return quader_geometry::plane_from_point_normal(to_box_geometry_vec3(origin), to_box_geometry_vec3(normal), tool_epsilon);
}

quader::QVec3 project_box_poly_point_to_plane(quader::QVec3 point, const quader_geometry::QPlane3<double>& plane)
{
	return to_box_poly_vec3(quader_geometry::project_point_to_plane(to_box_geometry_vec3(point), plane));
}

quader::QVec3 box_geometry_plane_point_to_poly(const quader_geometry::QPlaneBasis3<double>& basis, double u, double v)
{
	return to_box_poly_vec3(quader_geometry::plane_point_from_coordinates(basis, u, v));
}

quader_geometry::QPlaneBasis3<double> box_geometry_basis(const BoxConstructionPlane& plane)
{
	quader_geometry::QPlaneBasis3<double> basis;
	basis.valid = !vec3_is_tiny(plane.normal) && !vec3_is_tiny(plane.axis_u) && !vec3_is_tiny(plane.axis_v);
	basis.origin = to_box_geometry_vec3(plane.snap_origin);
	basis.normal = to_box_geometry_vec3(plane.normal);
	basis.u = to_box_geometry_vec3(plane.axis_u);
	basis.v = to_box_geometry_vec3(plane.axis_v);
	return basis;
}

ToolStatus box_ok_status(bool changed = false)
{
	return { true, changed, {} };
}

BoxToolStateResult box_result(const BoxToolState& state, ToolStatus status)
{
	return { status.ok, status.changed, std::move(status.message), state };
}

double box_grid_or_default(double grid_size)
{
	return std::isfinite(grid_size) && grid_size > tool_epsilon ? grid_size : 1.0;
}

/**
 * Implements Box Tool behavior for the modal modeling interaction layer.
 */
class BoxTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::box;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Box";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		reset_editor_runtime_box_state(runtime);
	}

	[[nodiscard]] quader::QVec3 project_point_to_plane(
			quader::QVec3 point,
			quader::QVec3 origin,
			quader::QVec3 normal) const
	{
		const quader::QVec3 safe_normal = vec3_is_tiny(normal) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(normal);
		return project_box_poly_point_to_plane(point, to_box_geometry_plane(origin, safe_normal));
	}

	[[nodiscard]] BoxConstructionPlane normalize_construction_plane(quader::QVec3 origin, quader::QVec3 normal) const
	{
		BoxConstructionPlane plane;
		plane.origin = origin;
		plane.normal = vec3_is_tiny(normal) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(normal);
		plane.snap_origin = project_point_to_plane({}, plane.origin, plane.normal);
		quader::QVec3 preferred { 1.0F, 0.0F, 0.0F };
		if (std::abs(vec3_dot(plane.normal, preferred)) > 0.9) {
			preferred = { 0.0F, 0.0F, 1.0F };
		}
		const quader_geometry::QPlaneBasis3<double> basis = quader_geometry::make_plane_basis(
				to_box_geometry_vec3(plane.origin),
				to_box_geometry_vec3(plane.normal),
				to_box_geometry_vec3(preferred),
				tool_epsilon);
		if (basis.valid) {
			plane.axis_u = to_box_poly_vec3(basis.u);
			plane.axis_v = to_box_poly_vec3(basis.v);
		}
		return plane;
	}

	[[nodiscard]] BoxToolStateResult set_construction_plane(
			BoxToolState& state,
			quader::QVec3 origin,
			quader::QVec3 normal) const
	{
		state.plane = normalize_construction_plane(origin, normal);
		return box_result(state, box_ok_status(true));
	}

	[[nodiscard]] BoxConstructionPointResult construction_point_from_ray(
			const BoxConstructionPlane& plane,
			const quader_poly::Ray& ray) const
	{
		if (vec3_is_tiny(ray.direction)) {
			return { false, "Invalid construction ray.", {}, 0.0 };
		}
		const quader_geometry::QPlane3<double> geometry_plane = to_box_geometry_plane(plane.origin, plane.normal);
		const quader_geometry::QRayPlaneHit<double> ray_hit =
			quader_geometry::intersect_ray_plane(to_box_geometry_ray(ray), geometry_plane, tool_epsilon);
		if (!ray_hit.hit) {
			const quader_geometry::QLinePlaneHit<double> line_hit =
				quader_geometry::intersect_line_plane(to_box_geometry_line(ray), geometry_plane, tool_epsilon);
			if (line_hit.type != quader_geometry::QPlaneIntersectionType::point) {
				return { false, "Construction ray is parallel to the plane.", {}, 0.0 };
			}
			return { false, "Construction plane is behind the ray.", {}, line_hit.parameter };
		}
		const quader::QVec3 point = to_box_poly_vec3(quader_geometry::project_point_to_plane(ray_hit.point, geometry_plane));
		return { true, {}, point, ray_hit.distance };
	}

	[[nodiscard]] quader::QVec2 directional_snap_pair(double start_value, double end_value, double grid_size) const
	{
		const double grid = box_grid_or_default(grid_size);
		if (end_value >= start_value) {
			const double start_snap = std::floor(start_value / grid) * grid;
			double end_snap = std::ceil(end_value / grid) * grid;
			if (end_snap - start_snap <= min_box_size) {
				end_snap = start_snap + grid;
			}
			return { static_cast<float>(start_snap), static_cast<float>(end_snap) };
		}

		const double start_snap = std::ceil(start_value / grid) * grid;
		double end_snap = std::floor(end_value / grid) * grid;
		if (start_snap - end_snap <= min_box_size) {
			end_snap = start_snap - grid;
		}
		return { static_cast<float>(start_snap), static_cast<float>(end_snap) };
	}

	[[nodiscard]] quader::QVec2 footprint_signed_size(
			quader::QVec3 start_point,
			quader::QVec3 end_point,
			quader::QVec3 axis_u,
			quader::QVec3 axis_v,
			double grid_size) const
	{
		const double grid = box_grid_or_default(grid_size);
		const quader::QVec3 delta = end_point - start_point;
		double u = vec3_dot(axis_u, delta);
		double v = vec3_dot(axis_v, delta);
		if (std::abs(u) <= min_box_size) {
			u = u >= 0.0 ? grid : -grid;
		}
		if (std::abs(v) <= min_box_size) {
			v = v >= 0.0 ? grid : -grid;
		}
		return { static_cast<float>(u), static_cast<float>(v) };
	}

	[[nodiscard]] std::vector<quader::QVec3> footprint_points(
			quader::QVec3 start_point,
			quader::QVec3 end_point,
			quader::QVec3 axis_u,
			quader::QVec3 axis_v,
			double grid_size) const
	{
		const quader::QVec2 size = footprint_signed_size(start_point, end_point, axis_u, axis_v, grid_size);
		const quader::QVec3 u = vec3_scale(axis_u, size.x);
		const quader::QVec3 v = vec3_scale(axis_v, size.y);
		return {
			start_point,
			start_point + v,
			start_point + u + v,
			start_point + u,
		};
	}

	[[nodiscard]] BoxFootprintDescriptor footprint_descriptor(
			quader::QVec3 start_point,
			quader::QVec3 end_point,
			quader::QVec3 axis_u,
			quader::QVec3 axis_v,
			double grid_size) const
	{
		BoxFootprintDescriptor descriptor;
		descriptor.ok = true;
		descriptor.signed_size = footprint_signed_size(start_point, end_point, axis_u, axis_v, grid_size);
		descriptor.size = { static_cast<float>(std::abs(descriptor.signed_size.x)), static_cast<float>(std::abs(descriptor.signed_size.y)) };
		descriptor.footprint = footprint_points(start_point, end_point, axis_u, axis_v, grid_size);
		for (const quader::QVec3& point : descriptor.footprint) {
			descriptor.center += point;
		}
		if (!descriptor.footprint.empty()) {
			descriptor.center = descriptor.center / static_cast<float>(descriptor.footprint.size());
		}
		return descriptor;
	}

	[[nodiscard]] double snap_height_distance(double distance, bool grid_enabled, double grid_size) const
	{
		const double grid = box_grid_or_default(grid_size);
		if (grid_enabled) {
			distance = std::round(distance / grid) * grid;
		}
		if (std::abs(distance) <= min_box_size) {
			distance = distance >= 0.0 ? grid : -grid;
		}
		return distance;
	}

	[[nodiscard]] BoxToolStateResult begin_footprint(
			BoxToolState& state,
			quader::QVec3 raw_start,
			quader::QVec3 raw_end,
			bool grid_enabled,
			double grid_size) const
	{
		state.drag_state = BoxDragState::footprint;
		state.raw_start_point = raw_start;
		state.raw_end_point = raw_end;
		state.hover_cell_points.clear();
		state.preview_brush_id = -1;
		state.preview_valid = false;
		return update_footprint(state, raw_end, grid_enabled, grid_size);
	}

	[[nodiscard]] BoxToolStateResult update_footprint(
			BoxToolState& state,
			quader::QVec3 raw_end,
			bool grid_enabled,
			double grid_size) const
	{
		state.raw_end_point = raw_end;
		if (!grid_enabled) {
			const quader::QVec3 projected_start = project_point_to_plane(state.raw_start_point, state.plane.origin, state.plane.normal);
			const quader::QVec3 projected_end = project_point_to_plane(state.raw_end_point, state.plane.origin, state.plane.normal);
			state.start_point = projected_start;
			state.end_point = projected_end;
			return box_result(state, box_ok_status(true));
		}

		const double grid = box_grid_or_default(grid_size);
		const quader_geometry::QPlaneBasis3<double> basis = box_geometry_basis(state.plane);
		const quader_geometry::QPlaneCoordinates3<double> raw_start_coords =
			quader_geometry::plane_coordinates(to_box_geometry_vec3(state.raw_start_point), basis);
		const quader_geometry::QPlaneCoordinates3<double> raw_end_coords =
			quader_geometry::plane_coordinates(to_box_geometry_vec3(state.raw_end_point), basis);
		const quader::QVec2 u_pair = directional_snap_pair(raw_start_coords.u, raw_end_coords.u, grid);
		const quader::QVec2 v_pair = directional_snap_pair(raw_start_coords.v, raw_end_coords.v, grid);
		state.start_point = box_geometry_plane_point_to_poly(basis, u_pair.x, v_pair.x);
		state.end_point = box_geometry_plane_point_to_poly(basis, u_pair.y, v_pair.y);
		return box_result(state, box_ok_status(true));
	}

	[[nodiscard]] BoxToolStateResult update_hover_cell(
			BoxToolState& state,
			quader::QVec3 local_point,
			bool grid_enabled,
			double grid_size) const
	{
		state.raw_start_point = local_point;
		state.raw_end_point = local_point;
		const BoxDragState previous_state = state.drag_state;
		state.drag_state = BoxDragState::idle;
		BoxToolStateResult result = update_footprint(state, local_point, grid_enabled, grid_size);
		state.drag_state = previous_state == BoxDragState::idle ? BoxDragState::idle : previous_state;
		state.hover_cell_points = footprint_points(state.start_point, state.end_point, state.plane.axis_u, state.plane.axis_v, grid_size);
		result.state = state;
		return result;
	}

	[[nodiscard]] BoxToolStateResult begin_height(
			BoxToolState& state,
			quader::QVec2 screen_position,
			double axis_distance,
			bool has_axis_distance,
			bool grid_enabled,
			double grid_size) const
	{
		state.drag_state = BoxDragState::height;
		state.height_mouse_start = screen_position;
		state.height_start_distance = snap_height_distance(state.height_distance, grid_enabled, grid_size);
		state.height_distance = state.height_start_distance;
		state.height_axis_start = has_axis_distance && std::isfinite(axis_distance) ? axis_distance : std::numeric_limits<double>::infinity();
		return box_result(state, box_ok_status(true));
	}

	[[nodiscard]] double height_distance_for_ray(
			const BoxToolState& state,
			const quader_poly::Ray& ray,
			double grid_size) const
	{
		if (vec3_is_tiny(ray.direction)) {
			return std::numeric_limits<double>::infinity();
		}
		const quader::QVec3 axis = vec3_is_tiny(state.plane.normal) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(state.plane.normal);
		const quader::QVec3 center = footprint_descriptor(state.start_point, state.end_point, state.plane.axis_u, state.plane.axis_v, grid_size).center;
		const DragDistanceResult distance = axis_distance_from_ray(axis, center, ray, screen_epsilon);
		return distance.ok ? distance.distance : std::numeric_limits<double>::infinity();
	}

	[[nodiscard]] BoxToolStateResult update_height_preview(
			BoxToolState& state,
			double axis_distance,
			bool has_axis_distance,
			quader::QVec2 screen_position,
			bool grid_enabled,
			double grid_size) const
	{
		double distance =
			state.height_start_distance +
			((state.height_mouse_start.y - screen_position.y) / box_height_pixels_per_grid_step) * box_grid_or_default(grid_size);
		if (has_axis_distance &&
				std::isfinite(axis_distance) &&
				std::isfinite(state.height_axis_start)) {
			distance = state.height_start_distance + axis_distance - state.height_axis_start;
		}
		state.height_distance = snap_height_distance(distance, grid_enabled, grid_size);
		return box_result(state, box_ok_status(true));
	}

	[[nodiscard]] BoxPreviewDescriptor preview_descriptor(
			const BoxToolState& state,
			bool grid_enabled,
			double grid_size) const
	{
		BoxPreviewDescriptor descriptor;
		descriptor.ok = true;
		descriptor.drag_state = state.drag_state;
		descriptor.footprint = footprint_descriptor(state.start_point, state.end_point, state.plane.axis_u, state.plane.axis_v, grid_size);
		descriptor.signed_height = snap_height_distance(state.height_distance, grid_enabled, grid_size);
		descriptor.normal = descriptor.signed_height >= 0.0 ? state.plane.normal : vec3_scale(state.plane.normal, -1.0);
		descriptor.distance = std::abs(descriptor.signed_height);
		quader_geometry::QAabb3<double> bounds = quader_geometry::empty_aabb3<double>();
		const auto include_bound = [&bounds](quader::QVec3 point) {
			quader_geometry::aabb_include(bounds, to_box_geometry_vec3(point));
		};
		for (const quader::QVec3& point : descriptor.footprint.footprint) {
			include_bound(point);
			if (descriptor.drag_state == BoxDragState::height) {
				include_bound(point + vec3_scale(descriptor.normal, descriptor.distance));
			}
		}
		descriptor.bounds = to_box_editor_bounds(bounds);
		descriptor.preview_brush_id = state.preview_brush_id;
		descriptor.preview_valid = state.preview_valid;
		return descriptor;
	}

	[[nodiscard]] std::vector<quader::QVec3> preview_corners(const BoxPreviewDescriptor& preview) const
	{
		if (!preview.ok || preview.drag_state != BoxDragState::height || preview.footprint.footprint.size() < 4U) {
			return {};
		}
		const quader::QVec3 height = vec3_scale(preview.normal, preview.distance);
		std::vector<quader::QVec3> corners;
		corners.reserve(8);
		for (std::size_t index = 0; index < 4U; ++index) {
			corners.push_back(preview.footprint.footprint[index]);
		}
		for (std::size_t index = 0; index < 4U; ++index) {
			corners.push_back(preview.footprint.footprint[index] + height);
		}
		return corners;
	}

	void set_preview_state(BoxToolState& state, int preview_brush_id, bool preview_valid) const
	{
		state.preview_brush_id = preview_brush_id;
		state.preview_valid = preview_valid;
	}

	void clear_preview_state(BoxToolState& state) const
	{
		state.preview_brush_id = -1;
		state.preview_valid = false;
	}

	void sync_state(BoxToolState& state, const BoxToolState& source) const
	{
		state = source;
		state.plane.normal = vec3_is_tiny(state.plane.normal) ? quader::QVec3 { 0.0F, 1.0F, 0.0F } : normalized_axis(state.plane.normal);
		state.plane.axis_u = vec3_is_tiny(state.plane.axis_u) ? quader::QVec3 { 1.0F, 0.0F, 0.0F } : normalized_axis(state.plane.axis_u);
		state.plane.axis_v = vec3_is_tiny(state.plane.axis_v) ? quader::QVec3 { 0.0F, 0.0F, 1.0F } : normalized_axis(state.plane.axis_v);
		state.height_axis_start = std::isfinite(state.height_axis_start) ? state.height_axis_start : std::numeric_limits<double>::infinity();
	}

	void reset(BoxToolState& state) const
	{
		state = BoxToolState();
	}
};

const BoxTool& box_tool_instance()
{
	static const BoxTool tool;
	return tool;
}

} // namespace

const EditorTool& box_tool()
{
	return box_tool_instance();
}

quader::QVec3 project_box_point_to_plane(quader::QVec3 point, quader::QVec3 origin, quader::QVec3 normal)
{
	return box_tool_instance().project_point_to_plane(point, origin, normal);
}

BoxConstructionPlane normalize_box_construction_plane(quader::QVec3 origin, quader::QVec3 normal)
{
	return box_tool_instance().normalize_construction_plane(origin, normal);
}

BoxToolStateResult set_box_construction_plane(BoxToolState& state, quader::QVec3 origin, quader::QVec3 normal)
{
	return box_tool_instance().set_construction_plane(state, origin, normal);
}

BoxConstructionPointResult box_construction_point_from_ray(const BoxConstructionPlane& plane, const quader_poly::Ray& ray)
{
	return box_tool_instance().construction_point_from_ray(plane, ray);
}

quader::QVec2 box_directional_snap_pair(double start_value, double end_value, double grid_size)
{
	return box_tool_instance().directional_snap_pair(start_value, end_value, grid_size);
}

quader::QVec2 box_footprint_signed_size(
		quader::QVec3 start_point,
		quader::QVec3 end_point,
		quader::QVec3 axis_u,
		quader::QVec3 axis_v,
		double grid_size)
{
	return box_tool_instance().footprint_signed_size(start_point, end_point, axis_u, axis_v, grid_size);
}

std::vector<quader::QVec3> box_footprint_points(
		quader::QVec3 start_point,
		quader::QVec3 end_point,
		quader::QVec3 axis_u,
		quader::QVec3 axis_v,
		double grid_size)
{
	return box_tool_instance().footprint_points(start_point, end_point, axis_u, axis_v, grid_size);
}

BoxFootprintDescriptor box_footprint_descriptor(
		quader::QVec3 start_point,
		quader::QVec3 end_point,
		quader::QVec3 axis_u,
		quader::QVec3 axis_v,
		double grid_size)
{
	return box_tool_instance().footprint_descriptor(start_point, end_point, axis_u, axis_v, grid_size);
}

double snap_box_height_distance(double distance, bool grid_enabled, double grid_size)
{
	return box_tool_instance().snap_height_distance(distance, grid_enabled, grid_size);
}

BoxToolStateResult begin_box_footprint(
		BoxToolState& state,
		quader::QVec3 raw_start,
		quader::QVec3 raw_end,
		bool grid_enabled,
		double grid_size)
{
	return box_tool_instance().begin_footprint(state, raw_start, raw_end, grid_enabled, grid_size);
}

BoxToolStateResult update_box_footprint(
		BoxToolState& state,
		quader::QVec3 raw_end,
		bool grid_enabled,
		double grid_size)
{
	return box_tool_instance().update_footprint(state, raw_end, grid_enabled, grid_size);
}

BoxToolStateResult update_box_hover_cell(
		BoxToolState& state,
		quader::QVec3 local_point,
		bool grid_enabled,
		double grid_size)
{
	return box_tool_instance().update_hover_cell(state, local_point, grid_enabled, grid_size);
}

BoxToolStateResult begin_box_height(
		BoxToolState& state,
		quader::QVec2 screen_position,
		double axis_distance,
		bool has_axis_distance,
		bool grid_enabled,
		double grid_size)
{
	return box_tool_instance().begin_height(state, screen_position, axis_distance, has_axis_distance, grid_enabled, grid_size);
}

double box_height_distance_for_ray(const BoxToolState& state, const quader_poly::Ray& ray, double grid_size)
{
	return box_tool_instance().height_distance_for_ray(state, ray, grid_size);
}

BoxToolStateResult update_box_height_preview(
		BoxToolState& state,
		double axis_distance,
		bool has_axis_distance,
		quader::QVec2 screen_position,
		bool grid_enabled,
		double grid_size)
{
	return box_tool_instance().update_height_preview(state, axis_distance, has_axis_distance, screen_position, grid_enabled, grid_size);
}

BoxPreviewDescriptor box_preview_descriptor(const BoxToolState& state, bool grid_enabled, double grid_size)
{
	return box_tool_instance().preview_descriptor(state, grid_enabled, grid_size);
}

std::vector<quader::QVec3> box_preview_corners(const BoxPreviewDescriptor& preview)
{
	return box_tool_instance().preview_corners(preview);
}

void set_box_preview_state(BoxToolState& state, int preview_brush_id, bool preview_valid)
{
	box_tool_instance().set_preview_state(state, preview_brush_id, preview_valid);
}

void clear_box_preview_state(BoxToolState& state)
{
	box_tool_instance().clear_preview_state(state);
}

void sync_box_state(BoxToolState& state, const BoxToolState& source)
{
	box_tool_instance().sync_state(state, source);
}

void reset_box(BoxToolState& state)
{
	box_tool_instance().reset(state);
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/extrude_tool.hpp>

#include <editor/modeling/runtime/editor_tool.hpp>
#include <editor/interactions/tool_drag_math.hpp>
#include <editor/interactions/tool_transform_math.hpp>
#include <editor/modeling/runtime/tool_constants.hpp>

#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace quader_editor_core {
namespace {

quader_geometry::QVec2d to_geometry_vec2(quader::QVec2 value)
{
	return { value.x, value.y };
}

quader_geometry::QVec3d to_geometry_vec3(quader::QVec3 value)
{
	return { value.x, value.y, value.z };
}

quader::QVec2 to_poly_vec2(quader_geometry::QVec2d value)
{
	return { static_cast<float>(value.x), static_cast<float>(value.y) };
}

quader::QVec3 to_poly_vec3(quader_geometry::QVec3d value)
{
	return { static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z) };
}

bool vec3_is_tiny(quader::QVec3 value, double epsilon = tool_epsilon)
{
	return quader_geometry::length_squared(to_geometry_vec3(value)) <= epsilon * epsilon;
}

quader::QVec2 vec2_normalize_or(quader::QVec2 value, quader::QVec2 fallback)
{
	const quader_geometry::QVec2d geometry_value = to_geometry_vec2(value);
	const double length_squared = quader_geometry::length_squared(geometry_value);
	if (length_squared <= screen_epsilon) {
		return fallback;
	}
	return to_poly_vec2(geometry_value / std::sqrt(length_squared));
}

quader::QVec3 normalized_axis(quader::QVec3 value)
{
	return to_poly_vec3(quader_geometry::normalize_or_zero(to_geometry_vec3(value), tool_epsilon));
}

double safe_grid_size(double grid_size, double minimum)
{
	return std::max(grid_size, minimum);
}

/**
 * Represents an Extrude Tool Status value used by the modal modeling interaction layer.
 */
struct ExtrudeToolStatus {
	bool ok = true;
	bool changed = false;
	std::string message;
};

ExtrudeToolStatus ok_status(bool changed = false)
{
	return { true, changed, {} };
}

ExtrudeToolStatus error_status(std::string message)
{
	return { false, false, std::move(message) };
}

int clamp_cut_count(int value, int maximum)
{
	return std::min(std::max(value, 0), std::max(maximum, 0));
}

int max_cut_count_for_distance(double distance, bool grid_enabled, double grid_size)
{
	if (std::abs(distance) <= min_extrude_distance || !grid_enabled || grid_size <= min_extrude_distance) {
		return 0;
	}
	return std::max(0, static_cast<int>(std::floor((std::abs(distance) + min_extrude_distance) / grid_size)) - 1);
}

ExtrudeStateResult extrude_result(const ExtrudeToolState& state, ExtrudeToolStatus status)
{
	return { status.ok, status.changed, std::move(status.message), state };
}

/**
 * Implements Extrude Tool behavior for the modal modeling interaction layer.
 */
class ExtrudeTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::extrude;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Extrude";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		clear_editor_runtime_transform_drag_state(runtime);
		clear_editor_runtime_gizmo_hover(runtime);
	}

	[[nodiscard]] ExtrudeStateResult begin_extrude_drag(const ExtrudeBeginInput& input) const
	{
		ExtrudeToolState state;
		state.active = true;
		state.source_object_id = input.source_object_id;
		state.source_face_index = input.source_face_index;
		state.face_normal = vec3_is_tiny(input.face_normal, min_extrude_distance) ? quader::QVec3() : normalized_axis(input.face_normal);
		state.face_vertices = input.face_vertices;
		state.face_vertex_indices = input.face_vertex_indices;
		state.face_center = input.face_center;
		state.screen_axis = vec2_normalize_or(input.screen_axis, { 0.0F, 1.0F });
		return extrude_result(state, ok_status());
	}

	[[nodiscard]] double snap_extrude_distance(double distance, bool precision, bool grid_enabled, double grid_size) const
	{
		double snapped = precision ? distance * 0.25 : distance;
		if (grid_enabled) {
			const double grid = safe_grid_size(grid_size, min_extrude_distance);
			snapped = std::round(snapped / grid) * grid;
		}
		return snapped;
	}

	[[nodiscard]] double extrude_distance_for_drag_pixels(double pixels, bool precision, bool grid_enabled, double grid_size) const
	{
		return snap_extrude_distance((pixels / extrude_pixels_per_grid_step) * grid_size, precision, grid_enabled, grid_size);
	}

	[[nodiscard]] ExtrudeStateResult set_extrude_preview_distance(ExtrudeToolState& state, double distance, bool grid_enabled, double grid_size) const
	{
		if (!state.active) {
			return extrude_result(state, error_status("Extrude tool is not active."));
		}
		state.preview_distance = distance;
		state.max_cut_count = max_cut_count_for_distance(state.preview_distance, grid_enabled, grid_size);
		state.requested_cut_count = clamp_cut_count(state.requested_cut_count, state.max_cut_count);
		state.active_cut_count = std::abs(state.preview_distance) > min_extrude_distance && grid_enabled ? state.requested_cut_count : 0;
		return extrude_result(state, ok_status());
	}

	[[nodiscard]] ExtrudeStateResult preview_extrude_from_distance(ExtrudeToolState& state, double distance, bool precision, bool grid_enabled, double grid_size) const
	{
		state.drag_distance = distance;
		state.precision_mode = precision;
		return set_extrude_preview_distance(state, snap_extrude_distance(state.drag_distance, state.precision_mode, grid_enabled, grid_size), grid_enabled, grid_size);
	}

	[[nodiscard]] ExtrudeStateResult preview_extrude_from_drag_pixels(ExtrudeToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size) const
	{
		state.drag_pixels = pixels;
		state.precision_mode = precision;
		return set_extrude_preview_distance(state, extrude_distance_for_drag_pixels(state.drag_pixels, state.precision_mode, grid_enabled, grid_size), grid_enabled, grid_size);
	}

	[[nodiscard]] ExtrudeStateResult adjust_extrude_cut_count(ExtrudeToolState& state, int delta, bool grid_enabled, double grid_size) const
	{
		if (!state.active) {
			return extrude_result(state, error_status("Extrude tool is not active."));
		}
		if (!grid_enabled) {
			return extrude_result(state, error_status("Extrude cuts require grid snapping."));
		}
		const int previous = state.requested_cut_count;
		state.max_cut_count = max_cut_count_for_distance(state.preview_distance, grid_enabled, grid_size);
		state.requested_cut_count = clamp_cut_count(state.requested_cut_count + delta, state.max_cut_count);
		state.active_cut_count = std::abs(state.preview_distance) > min_extrude_distance ? state.requested_cut_count : 0;
		return extrude_result(state, ok_status(state.requested_cut_count != previous));
	}

	[[nodiscard]] double extrude_axis_distance_from_ray(const ExtrudeToolState& state, const quader_poly::Transform3& model_transform, const quader_poly::Ray& world_ray) const
	{
		if (!state.active || vec3_is_tiny(state.face_normal, min_extrude_distance) || vec3_is_tiny(world_ray.direction, min_extrude_distance)) {
			return std::numeric_limits<double>::infinity();
		}
		const quader::QVec3 local_ray_origin = inverse_transform_point(model_transform, world_ray.origin);
		quader::QVec3 local_ray_dir = inverse_transform_vector(model_transform, world_ray.direction);
		if (vec3_is_tiny(local_ray_dir, min_extrude_distance)) {
			return std::numeric_limits<double>::infinity();
		}
		const DragDistanceResult distance = axis_distance_from_ray(
				state.face_normal,
				state.face_center,
				{ local_ray_origin, local_ray_dir },
				screen_epsilon,
				min_extrude_distance);
		return distance.ok ? distance.distance : std::numeric_limits<double>::infinity();
	}

	[[nodiscard]] quader::QVec3 extrude_face_center_for_vertices(const std::vector<quader::QVec3>& vertices) const
	{
		if (vertices.empty()) {
			return {};
		}
		quader::QVec3 center;
		for (const quader::QVec3& vertex : vertices) {
			center += vertex;
		}
		return center / static_cast<float>(vertices.size());
	}

	void set_extrude_preview_valid(ExtrudeToolState& state, bool valid) const
	{
		state.preview_valid = valid;
	}

	[[nodiscard]] ExtrudePreviewOperation extrude_preview_operation(const ExtrudeToolState& state) const
	{
		ExtrudePreviewOperation descriptor;
		descriptor.ok = state.active;
		if (!state.active) {
			descriptor.message = "Extrude tool is not active.";
			return descriptor;
		}
		descriptor.preview_distance = state.preview_distance;
		descriptor.active_cut_count = state.active_cut_count;
		descriptor.max_cut_count = state.max_cut_count;
		descriptor.requested_cut_count = state.requested_cut_count;
		descriptor.select_source = true;
		if (std::abs(state.preview_distance) <= min_extrude_distance) {
			descriptor.preview_mode = ExtrudePreviewMode::zero;
			descriptor.clear_preview_meshes = true;
		} else if (state.preview_distance > min_extrude_distance) {
			descriptor.preview_mode = ExtrudePreviewMode::positive;
			descriptor.clear_preview_meshes = false;
		} else {
			descriptor.preview_mode = ExtrudePreviewMode::negative;
			descriptor.clear_preview_meshes = true;
		}
		return descriptor;
	}

	[[nodiscard]] ExtrudePreviewStatus extrude_preview_status(const ExtrudeToolState& state, bool grid_enabled) const
	{
		ExtrudePreviewStatus descriptor;
		descriptor.ok = state.active;
		descriptor.status_action = state.preview_distance >= 0.0 ? "Extrude" : "Reduce";
		descriptor.status_distance = std::abs(state.preview_distance);
		descriptor.show_cut_status = std::abs(state.preview_distance) > min_extrude_distance;
		descriptor.active_cut_count = state.active_cut_count;
		descriptor.max_cut_count = state.max_cut_count;
		descriptor.grid_enabled = grid_enabled;
		descriptor.preview_valid = state.preview_valid;
		return descriptor;
	}

	void reset_extrude(ExtrudeToolState& state) const
	{
		state = ExtrudeToolState();
	}
};

const ExtrudeTool& extrude_tool_impl()
{
	static const ExtrudeTool tool;
	return tool;
}

} // namespace

const EditorTool& extrude_tool()
{
	return extrude_tool_impl();
}

ExtrudeStateResult begin_extrude_drag(const ExtrudeBeginInput& input)
{
	return extrude_tool_impl().begin_extrude_drag(input);
}

double snap_extrude_distance(double distance, bool precision, bool grid_enabled, double grid_size)
{
	return extrude_tool_impl().snap_extrude_distance(distance, precision, grid_enabled, grid_size);
}

double extrude_distance_for_drag_pixels(double pixels, bool precision, bool grid_enabled, double grid_size)
{
	return extrude_tool_impl().extrude_distance_for_drag_pixels(pixels, precision, grid_enabled, grid_size);
}

ExtrudeStateResult set_extrude_preview_distance(ExtrudeToolState& state, double distance, bool grid_enabled, double grid_size)
{
	return extrude_tool_impl().set_extrude_preview_distance(state, distance, grid_enabled, grid_size);
}

ExtrudeStateResult preview_extrude_from_distance(ExtrudeToolState& state, double distance, bool precision, bool grid_enabled, double grid_size)
{
	return extrude_tool_impl().preview_extrude_from_distance(state, distance, precision, grid_enabled, grid_size);
}

ExtrudeStateResult preview_extrude_from_drag_pixels(ExtrudeToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size)
{
	return extrude_tool_impl().preview_extrude_from_drag_pixels(state, pixels, precision, grid_enabled, grid_size);
}

ExtrudeStateResult adjust_extrude_cut_count(ExtrudeToolState& state, int delta, bool grid_enabled, double grid_size)
{
	return extrude_tool_impl().adjust_extrude_cut_count(state, delta, grid_enabled, grid_size);
}

double extrude_axis_distance_from_ray(const ExtrudeToolState& state, const quader_poly::Transform3& model_transform, const quader_poly::Ray& world_ray)
{
	return extrude_tool_impl().extrude_axis_distance_from_ray(state, model_transform, world_ray);
}

quader::QVec3 extrude_face_center_for_vertices(const std::vector<quader::QVec3>& vertices)
{
	return extrude_tool_impl().extrude_face_center_for_vertices(vertices);
}

void set_extrude_preview_valid(ExtrudeToolState& state, bool valid)
{
	extrude_tool_impl().set_extrude_preview_valid(state, valid);
}

ExtrudePreviewOperation extrude_preview_operation(const ExtrudeToolState& state)
{
	return extrude_tool_impl().extrude_preview_operation(state);
}

ExtrudePreviewStatus extrude_preview_status(const ExtrudeToolState& state, bool grid_enabled)
{
	return extrude_tool_impl().extrude_preview_status(state, grid_enabled);
}

void reset_extrude(ExtrudeToolState& state)
{
	extrude_tool_impl().reset_extrude(state);
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/extend_tool.h>

#include <editor/modeling/runtime/editor_tool.h>

#include "tool_math_internal.h"

#include <cmath>
#include <string_view>

namespace quader_editor_core {
namespace {

/**
 * Implements Extend Tool behavior for the modal modeling interaction layer.
 */
class ExtendTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::extend;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Extend";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		clear_editor_runtime_transform_drag_state(runtime);
		clear_editor_runtime_gizmo_hover(runtime);
	}

	[[nodiscard]] ExtendStateResult begin_drag(const ExtendBeginInput& input) const
	{
		if (vec3_is_tiny(input.face_normal)) {
			return { false, false, "Invalid extend face normal.", {} };
		}
		ExtendToolState state;
		state.active = true;
		state.source_object_id = input.source_object_id;
		state.source_face_index = input.source_face_index;
		state.face_center = input.face_center;
		state.face_normal = normalized_axis(input.face_normal);
		state.screen_axis = vec2_normalize_or(input.screen_axis, { 1.0F, 0.0F });
		state.preview_distance = 0.0;
		state.preview_delta = {};
		return { true, false, {}, state };
	}

	[[nodiscard]] double snap_distance(double distance, bool precision, bool grid_enabled, double grid_size) const
	{
		return snap_move_distance(distance, precision, grid_enabled, grid_size);
	}

	[[nodiscard]] double distance_for_drag_pixels(double pixels, bool precision, bool grid_enabled, double grid_size) const
	{
		return snap_distance(raw_drag_distance(pixels, grid_size), precision, grid_enabled, grid_size);
	}

	[[nodiscard]] ExtendStateResult preview_from_distance(ExtendToolState& state, double distance, bool precision, bool grid_enabled, double grid_size) const
	{
		if (!state.active) {
			return { false, false, "Extend tool is not active.", state };
		}
		state.preview_distance = snap_distance(distance, precision, grid_enabled, grid_size);
		state.preview_delta = vec3_scale(state.face_normal, state.preview_distance);
		return { true, true, {}, state };
	}

	[[nodiscard]] ExtendStateResult preview_from_drag_pixels(ExtendToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size) const
	{
		if (!state.active) {
			return { false, false, "Extend tool is not active.", state };
		}
		return preview_from_distance(state, raw_drag_distance(pixels, grid_size), precision, grid_enabled, grid_size);
	}

	[[nodiscard]] quader::QVec3 handle_axis_for_normal(quader::QVec3 normal) const
	{
		if (vec3_is_tiny(normal)) {
			return { 0.0F, 1.0F, 0.0F };
		}
		const quader::QVec3 direction = normalized_axis(normal);
		const double abs_x = std::abs(direction.x);
		const double abs_y = std::abs(direction.y);
		const double abs_z = std::abs(direction.z);
		if (abs_y >= abs_x && abs_y >= abs_z) {
			return { 0.0F, direction.y < 0.0F ? -1.0F : 1.0F, 0.0F };
		}
		if (abs_x >= abs_z) {
			return { direction.x < 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F };
		}
		return { 0.0F, 0.0F, direction.z < 0.0F ? -1.0F : 1.0F };
	}

	[[nodiscard]] ExtendHandlePrimitives build_handle_primitives(const ExtendHandlePrimitiveInput& input) const
	{
		ExtendHandlePrimitives primitives;
		if (vec3_is_tiny(input.normal)) {
			return primitives;
		}
		primitives.ok = true;
		primitives.center = input.center;
		primitives.normal = normalized_axis(input.normal);
		primitives.handle_axis = handle_axis_for_normal(primitives.normal);
		primitives.handle_start = input.screen_center;
		primitives.ring_center = input.screen_center;
		primitives.ring_radius = input.ring_radius;

		const quader::QVec3 camera_direction = viewport_camera_forward(input.camera_transform);
		const bool axis_parallel_to_camera = std::abs(vec3_dot(normalized_axis(primitives.handle_axis), camera_direction)) >= input.camera_parallel_dot;
		quader::QVec2 screen_axis = input.has_projected_axis_delta ? input.projected_axis_delta : quader::QVec2();
		if (vec2_is_tiny(screen_axis) && !axis_parallel_to_camera) {
			screen_axis = {
				static_cast<float>(vec3_dot(primitives.handle_axis, input.camera_transform.x_axis)),
				static_cast<float>(-vec3_dot(primitives.handle_axis, input.camera_transform.y_axis)),
			};
		}
		primitives.stick_visible = !axis_parallel_to_camera;
		if (vec2_is_tiny(screen_axis)) {
			primitives.screen_axis = { 1.0F, 0.0F };
			primitives.stick_visible = false;
		} else {
			primitives.screen_axis = vec2_normalize_or(screen_axis, { 1.0F, 0.0F });
		}
		primitives.handle_center = input.screen_center;
		if (primitives.stick_visible) {
			primitives.handle_center = {
				static_cast<float>(static_cast<double>(input.screen_center.x) + static_cast<double>(primitives.screen_axis.x) * input.handle_screen_radius),
				static_cast<float>(static_cast<double>(input.screen_center.y) + static_cast<double>(primitives.screen_axis.y) * input.handle_screen_radius),
			};
		}
		primitives.handle_end = primitives.handle_center;
		return primitives;
	}

	void reset(ExtendToolState& state) const
	{
		state = ExtendToolState();
	}

private:
	[[nodiscard]] static double raw_drag_distance(double pixels, double grid_size)
	{
		return (pixels / move_pixels_per_grid_step) * safe_grid_size(grid_size, tool_epsilon);
	}
};

const ExtendTool& extend_tool_impl()
{
	static const ExtendTool tool;
	return tool;
}

} // namespace

const EditorTool& extend_tool()
{
	return extend_tool_impl();
}

ExtendStateResult begin_extend_drag(const ExtendBeginInput& input)
{
	return extend_tool_impl().begin_drag(input);
}

double snap_extend_distance(double distance, bool precision, bool grid_enabled, double grid_size)
{
	return extend_tool_impl().snap_distance(distance, precision, grid_enabled, grid_size);
}

double extend_distance_for_drag_pixels(double pixels, bool precision, bool grid_enabled, double grid_size)
{
	return extend_tool_impl().distance_for_drag_pixels(pixels, precision, grid_enabled, grid_size);
}

ExtendStateResult preview_extend_from_distance(ExtendToolState& state, double distance, bool precision, bool grid_enabled, double grid_size)
{
	return extend_tool_impl().preview_from_distance(state, distance, precision, grid_enabled, grid_size);
}

ExtendStateResult preview_extend_from_drag_pixels(ExtendToolState& state, double pixels, bool precision, bool grid_enabled, double grid_size)
{
	return extend_tool_impl().preview_from_drag_pixels(state, pixels, precision, grid_enabled, grid_size);
}

quader::QVec3 extend_handle_axis_for_normal(quader::QVec3 normal)
{
	return extend_tool_impl().handle_axis_for_normal(normal);
}

ExtendHandlePrimitives build_extend_handle_primitives(const ExtendHandlePrimitiveInput& input)
{
	return extend_tool_impl().build_handle_primitives(input);
}

void reset_extend(ExtendToolState& state)
{
	extend_tool_impl().reset(state);
}

} // namespace quader_editor_core

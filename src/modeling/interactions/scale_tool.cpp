////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/runtime/editor_tool.h>
#include <modeling/interactions/scale_tool.h>

#include "tool_math_internal.h"

#include <algorithm>
#include <string_view>

namespace quader_editor_core {
namespace {

/**
 * Implements Scale Tool behavior for the modal modeling interaction layer.
 */
class ScaleTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::scale;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Scale";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		clear_editor_runtime_transform_drag_state(runtime);
		clear_editor_runtime_gizmo_hover(runtime);
	}

	[[nodiscard]] ScaleBeginResult begin_scale_drag(const ScaleBeginInput& input) const
	{
		if (vec3_is_tiny(axis_vector(input.axis))) {
			return { false, "Invalid scale axis.", {} };
		}
		ScaleToolState state;
		state.active = true;
		state.axis = input.axis;
		state.object_ids = input.object_ids;
		state.pivot = input.pivot;
		state.screen_pivot = input.has_screen_pivot ? input.screen_pivot : input.screen_position;
		state.start_mouse = input.screen_position;
		const quader_poly::Vec2 screen_tip = input.has_screen_tip ?
			input.screen_tip :
			quader_poly::Vec2 { input.screen_position.x + 1.0F, input.screen_position.y };
		state.screen_axis = vec2_normalize_or(vec2_sub(screen_tip, state.screen_pivot), { 1.0F, 0.0F });
		state.bounds = input.bounds;
		state.preview_scale = { 1.0F, 1.0F, 1.0F };
		return { true, {}, state };
	}

	[[nodiscard]] ScalePreviewResult preview_scale_from_screen_position(
			ScaleToolState& state,
			quader_poly::Vec2 screen_position,
			bool grid_enabled,
			double grid_size) const
	{
		if (!state.active) {
			return { false, "Scale tool is not active.", {}, {}, {}, {} };
		}
		const double axis_pixels = vec2_dot(vec2_sub(screen_position, state.start_mouse), state.screen_axis);
		double factor = std::max(min_scale_factor, 1.0 + axis_pixels / scale_pixels_per_factor);
		factor = snapped_scale_factor(state.axis, factor, state.bounds, state.pivot, grid_enabled, grid_size);
		return preview_scale_from_scale(state, axis_scale(state.axis, factor));
	}

	[[nodiscard]] ScalePreviewResult preview_scale_from_scale(ScaleToolState& state, quader_poly::Vec3 scale) const
	{
		if (!state.active) {
			return { false, "Scale tool is not active.", {}, {}, {}, {} };
		}
		state.preview_scale = scale;
		const quader_poly::Transform3 transform = scale_transform(state.preview_scale);
		return { true, {}, state.preview_scale, state.preview_scale, transform, pivoted_transform(transform, state.pivot) };
	}

	void reset_scale(ScaleToolState& state) const
	{
		state = ScaleToolState();
	}
};

const ScaleTool& scale_tool_impl()
{
	static const ScaleTool tool;
	return tool;
}

} // namespace

const EditorTool& scale_tool()
{
	return scale_tool_impl();
}

ScaleBeginResult begin_scale_drag(const ScaleBeginInput& input)
{
	return scale_tool_impl().begin_scale_drag(input);
}

ScalePreviewResult preview_scale_from_screen_position(
		ScaleToolState& state,
		quader_poly::Vec2 screen_position,
		bool grid_enabled,
		double grid_size)
{
	return scale_tool_impl().preview_scale_from_screen_position(state, screen_position, grid_enabled, grid_size);
}

ScalePreviewResult preview_scale_from_scale(ScaleToolState& state, quader_poly::Vec3 scale)
{
	return scale_tool_impl().preview_scale_from_scale(state, scale);
}

void reset_scale(ScaleToolState& state)
{
	scale_tool_impl().reset_scale(state);
}

} // namespace quader_editor_core

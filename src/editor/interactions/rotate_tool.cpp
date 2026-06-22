////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/rotate_tool.h>

#include "tool_math_internal.h"

#include <editor/modeling/runtime/editor_tool.h>
#include <editor/interactions/transform_drag.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace quader_editor_core {
namespace {

/**
 * Implements Rotate Tool behavior for the modal modeling interaction layer.
 */
class RotateTool final : public EditorTool {
public:
	[[nodiscard]] ViewportInteractionTool tool_id() const override
	{
		return ViewportInteractionTool::rotate;
	}

	[[nodiscard]] std::string_view id() const override
	{
		return viewport_interaction_tool_id(tool_id());
	}

	[[nodiscard]] std::string_view label() const override
	{
		return "Rotate";
	}

	void reset_runtime_state(EditorRuntimeState& runtime, bool) const override
	{
		clear_editor_runtime_transform_drag_state(runtime);
		clear_editor_runtime_gizmo_hover(runtime);
	}

	[[nodiscard]] RotateBeginResult begin_rotate_drag(const RotateBeginInput& input) const
	{
		if (vec3_is_tiny(rotation_axis_vector(input.axis))) {
			return { false, "Invalid rotate axis.", {} };
		}
		RotateToolState state;
		state.active = true;
		state.axis = input.axis;
		state.object_ids = input.object_ids;
		state.pivot = input.pivot;
		state.screen_pivot = input.has_screen_pivot ? input.screen_pivot : input.screen_position;

		quader_poly::Vec2 start_vector = vec2_sub(input.screen_position, state.screen_pivot);
		if (vec2_is_tiny(start_vector)) {
			start_vector = { 1.0F, 0.0F };
		}
		state.start_angle = std::atan2(start_vector.y, start_vector.x);
		state.last_angle = state.start_angle;
		state.drag_view_sign = 1.0;
		if (input.has_camera) {
			quader_poly::Vec3 world_axis = input.has_world_direction ? input.world_direction : rotation_axis_vector(input.axis);
			if (vec3_is_tiny(world_axis)) {
				world_axis = rotation_axis_vector(input.axis);
			}
			const quader_poly::Vec3 world_pivot = input.has_world_pivot ? input.world_pivot : input.pivot;
			const quader_poly::Vec3 to_camera = input.camera_transform.origin - world_pivot;
			if (!vec3_is_tiny(world_axis) && !vec3_is_tiny(to_camera)) {
				state.drag_view_sign = vec3_dot(normalized_axis(world_axis), normalized_axis(to_camera)) >= 0.0 ? 1.0 : -1.0;
			}
		}
		return { true, {}, state };
	}

	[[nodiscard]] RotatePreviewResult preview_rotate_from_screen_position(
			RotateToolState& state,
			quader_poly::Vec2 screen_position,
			double snap_degrees) const
	{
		if (!state.active) {
			return { false, "Rotate tool is not active.", {}, {}, {}, {} };
		}
		const quader_poly::Vec2 current_vector = vec2_sub(screen_position, state.screen_pivot);
		if (vec2_is_tiny(current_vector)) {
			return preview_rotate_from_angle_degrees(state, state.accumulated_angle_degrees, snap_degrees);
		}
		const double current_angle = std::atan2(current_vector.y, current_vector.x);
		const double delta_degrees =
			-wrap_radians(current_angle - state.last_angle) * state.drag_view_sign * 180.0 / std::acos(-1.0);
		state.last_angle = current_angle;
		state.accumulated_angle_degrees += delta_degrees;
		return preview_rotate_from_angle_degrees(state, state.accumulated_angle_degrees, snap_degrees);
	}

	[[nodiscard]] RotatePreviewResult preview_rotate_from_angle_degrees(
			RotateToolState& state,
			double angle_degrees,
			double snap_degrees) const
	{
		if (!state.active) {
			return { false, "Rotate tool is not active.", {}, {}, {}, {} };
		}
		state.accumulated_angle_degrees = angle_degrees;
		if (snap_degrees > 0.0) {
			angle_degrees = std::round(angle_degrees / snap_degrees) * snap_degrees;
		}
		state.preview_angle_degrees = angle_degrees;
		const quader_poly::Transform3 transform = rotation_transform(state.axis, state.preview_angle_degrees * std::acos(-1.0) / 180.0);
		return { true, {}, state.preview_angle_degrees, state.preview_angle_degrees, transform, pivoted_transform(transform, state.pivot) };
	}

	void reset_rotate(RotateToolState& state) const
	{
		state = RotateToolState();
	}
};

const RotateTool& rotate_tool_impl()
{
	static const RotateTool tool;
	return tool;
}

} // namespace

const EditorTool& rotate_tool()
{
	return rotate_tool_impl();
}

RotateBeginResult begin_rotate_drag(const RotateBeginInput& input)
{
	return rotate_tool_impl().begin_rotate_drag(input);
}

RotatePreviewResult preview_rotate_from_screen_position(RotateToolState& state, quader_poly::Vec2 screen_position, double snap_degrees)
{
	return rotate_tool_impl().preview_rotate_from_screen_position(state, screen_position, snap_degrees);
}

RotatePreviewResult preview_rotate_from_angle_degrees(RotateToolState& state, double angle_degrees, double snap_degrees)
{
	return rotate_tool_impl().preview_rotate_from_angle_degrees(state, angle_degrees, snap_degrees);
}

void reset_rotate(RotateToolState& state)
{
	rotate_tool_impl().reset_rotate(state);
}

ModalTransformDragTool modal_transform_drag_tool_from_id(std::string_view tool_id)
{
	if (tool_id == "scale") {
		return ModalTransformDragTool::scale;
	}
	return ModalTransformDragTool::rotate;
}

std::string modal_transform_drag_tool_id(ModalTransformDragTool tool)
{
	if (tool == ModalTransformDragTool::scale) {
		return "scale";
	}
	return "rotate";
}

ModalTransformBeginPlan plan_modal_transform_drag_begin(const ModalTransformBeginInput& input)
{
	ModalTransformBeginPlan plan;
	plan.request_duplicate = input.duplicate_requested && input.can_duplicate_selection;
	if (input.duplicate_requested && !input.can_duplicate_selection) {
		plan.ok = false;
		plan.message = "Duplicate failed.";
	}
	return plan;
}

ModalTransformUpdatePlan plan_modal_transform_drag_update(const ModalTransformUpdateInput& input)
{
	ModalTransformUpdatePlan plan;
	if (!input.active) {
		return plan;
	}
	plan.handled = true;
	plan.preview_ok = input.preview_ok;
	if (!input.preview_ok) {
		plan.route = ModalTransformPreviewRoute::clear_preview;
		return plan;
	}
	const double epsilon = std::max(input.epsilon, 0.0);
	if (input.tool == ModalTransformDragTool::scale) {
		plan.preview_valid = scale_delta(input.preview_scale) > epsilon;
	} else {
		plan.preview_valid = std::abs(input.preview_angle_degrees) > epsilon;
	}
	plan.route = plan.preview_valid ? ModalTransformPreviewRoute::transform_preview : ModalTransformPreviewRoute::clear_preview;
	return plan;
}

ModalTransformCommitPlan plan_modal_transform_drag_commit(const ModalTransformCommitInput& input)
{
	ModalTransformCommitPlan plan;
	if (!input.active) {
		return plan;
	}
	plan.handled = true;
	if (!input.preview_valid) {
		plan.restore_base_snapshot = input.duplicate_active;
		plan.restore_selection = input.duplicate_active;
		return plan;
	}
	plan.model_changed = true;
	plan.route = ModalTransformCommitRoute::transform_patch;
	plan.undo_action = input.tool == ModalTransformDragTool::scale ? "Quader Scale Brushes" : "Quader Rotate Brushes";
	return plan;
}

} // namespace quader_editor_core

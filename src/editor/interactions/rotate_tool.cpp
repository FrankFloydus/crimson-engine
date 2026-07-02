////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/rotate_tool.hpp>

#include "tool_math_internal.hpp"

#include <editor/modeling/runtime/editor_tool.hpp>
#include <editor/interactions/transform_drag.hpp>

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
          return ViewportInteractionTool::Rotate;
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
		state.start_screen = input.screen_position;

		quader::QVec2 start_vector = vec2_sub(input.screen_position, state.screen_pivot);
		if (vec2_is_tiny(start_vector)) {
			start_vector = { 1.0F, 0.0F };
		}
		state.start_angle = std::atan2(start_vector.y, start_vector.x);
		state.last_angle = state.start_angle;
		state.drag_view_sign = 1.0;
		if (input.has_camera) {
			quader::QVec3 world_axis = input.has_world_direction ? input.world_direction : rotation_axis_vector(input.axis);
			if (vec3_is_tiny(world_axis)) {
				world_axis = rotation_axis_vector(input.axis);
			}
			const quader::QVec3 world_pivot = input.has_world_pivot ? input.world_pivot : input.pivot;
			const quader::QVec3 to_camera = input.camera_transform.origin - world_pivot;
			if (!vec3_is_tiny(world_axis) && !vec3_is_tiny(to_camera)) {
				world_axis = normalized_axis(world_axis);
				const quader::QVec3 view_to_camera = normalized_axis(to_camera);
				state.drag_view_sign = vec3_dot(world_axis, view_to_camera) >= 0.0 ? 1.0 : -1.0;

                                static constexpr double kEdgeOnThreshold =
                                    0.08715574274765817; // cos(85 degrees).
                                state.edge_on_linear_drag =
                                    std::abs(
                                        vec3_dot(world_axis, view_to_camera)) <
                                    kEdgeOnThreshold;
                                if (state.edge_on_linear_drag) {
					const quader::QVec3 camera_plane_normal = vec3_scale(view_to_camera, -1.0);
					const quader::QVec3 projection_axis = normalized_axis(vec3_cross(camera_plane_normal, world_axis));
					const quader::QVec2 projected_screen_axis {
						static_cast<float>(vec3_dot(projection_axis, normalized_axis(input.camera_transform.x_axis))),
						static_cast<float>(-vec3_dot(projection_axis, normalized_axis(input.camera_transform.y_axis))),
					};
					state.edge_on_screen_axis = vec2_normalize_or(projected_screen_axis, { 1.0F, 0.0F });

					const quader::QVec2 screen_tip = input.has_screen_tip ?
						input.screen_tip :
						vec2_add(state.screen_pivot, vec2_scale(start_vector, 1.0));
					state.edge_on_radius_pixels = std::sqrt(std::max(vec2_distance_squared(screen_tip, state.screen_pivot), 1.0));
				}
			}
		}
		return { true, {}, state };
	}

	[[nodiscard]] RotatePreviewResult preview_rotate_from_screen_position(
			RotateToolState& state,
			quader::QVec2 screen_position,
			double snap_degrees) const
	{
		if (!state.active) {
			return { false, "Rotate tool is not active.", {}, {}, {}, {} };
		}
		if (state.edge_on_linear_drag) {
			const double projected_pixels = vec2_dot(vec2_sub(screen_position, state.start_screen), state.edge_on_screen_axis);
			const double angle_degrees = projected_pixels * 90.0 / std::max(state.edge_on_radius_pixels, 1.0);
			return preview_rotate_from_angle_degrees(state, angle_degrees, snap_degrees);
		}
		const quader::QVec2 current_vector = vec2_sub(screen_position, state.screen_pivot);
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
  static const RotateTool kTool;
  return kTool;
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

RotatePreviewResult preview_rotate_from_screen_position(RotateToolState& state, quader::QVec2 screen_position, double snap_degrees)
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
          return ModalTransformDragTool::Scale;
        }
        return ModalTransformDragTool::Rotate;
}

std::string modal_transform_drag_tool_id(ModalTransformDragTool tool)
{
  if (tool == ModalTransformDragTool::Scale) {
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
          plan.route = ModalTransformPreviewRoute::ClearPreview;
          return plan;
	}
	const double epsilon = std::max(input.epsilon, 0.0);
        if (input.tool == ModalTransformDragTool::Scale) {
          plan.preview_valid = scale_delta(input.preview_scale) > epsilon;
        } else {
          plan.preview_valid = std::abs(input.preview_angle_degrees) > epsilon;
        }
        plan.route = plan.preview_valid
                         ? ModalTransformPreviewRoute::TransformPreview
                         : ModalTransformPreviewRoute::ClearPreview;
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
        plan.route = ModalTransformCommitRoute::TransformPatch;
        plan.undo_action = input.tool == ModalTransformDragTool::Scale
                               ? "Quader Scale Brushes"
                               : "Quader Rotate Brushes";
        return plan;
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.h>
#include <editor/interactions/tool_identity.h>

#include <cstddef>
#include <string_view>

namespace quader_editor_core {

/**
 * Stores the Viewport Tool Activation Request data contract used by the modeling runtime domain.
 */
struct ViewportToolActivationRequest {
	ViewportInteractionTool current_tool = ViewportInteractionTool::select;
	ViewportInteractionTool requested_tool = ViewportInteractionTool::select;
	SelectionMode selection_mode = SelectionMode::object;
	bool transform_drag_active = false;
	bool transform_preview_active = false;
	bool hover_has_object = false;
	bool hover_has_candidate_object = false;
	std::size_t selected_object_count = 0;
	std::size_t selected_cut_target_object_count = 0;
};

/**
 * Represents a Viewport Tool Activation Plan value used by the modeling runtime domain.
 */
struct ViewportToolActivationPlan {
	ViewportInteractionTool active_tool = ViewportInteractionTool::select;
	SelectionMode selection_mode = SelectionMode::object;
	bool active_tool_changed = false;
	bool selection_mode_changed = false;
	bool cancel_transform_preview = false;
	bool reset_transform_drag = false;
	bool reset_insert_edge_loop_tool = true;
	bool reset_knife_tool = true;
	bool reset_cut_tool = true;
	bool reset_mirror_tool = true;
	bool reset_poly_tool = true;
	bool insert_edge_loop_tool_active = false;
	bool knife_tool_active = false;
	bool cut_tool_active = false;
	bool mirror_tool_active = false;
	bool poly_tool_active = false;
	bool reset_box_tool = true;
	bool reset_box_grid_size = true;
	bool clear_gizmo_hover = true;
	bool promote_hover_object_to_candidate = false;
	bool activation_blocked = false;
	std::string_view blocked_message;
};

inline ViewportInteractionTool sanitize_viewport_interaction_tool(ViewportInteractionTool tool)
{
	return tool == ViewportInteractionTool::unknown ? ViewportInteractionTool::select : tool;
}

inline bool viewport_tool_uses_transform_drag(ViewportInteractionTool tool)
{
	return tool == ViewportInteractionTool::move ||
		tool == ViewportInteractionTool::rotate ||
		tool == ViewportInteractionTool::scale ||
		tool == ViewportInteractionTool::extend ||
		tool == ViewportInteractionTool::extrude;
}

inline bool viewport_tool_uses_transform_gizmo(ViewportInteractionTool tool)
{
	return tool == ViewportInteractionTool::move ||
		tool == ViewportInteractionTool::rotate ||
		tool == ViewportInteractionTool::scale;
}

inline bool viewport_tool_requires_selected_objects(ViewportInteractionTool tool)
{
	return tool == ViewportInteractionTool::cut ||
		tool == ViewportInteractionTool::mirror;
}

inline bool viewport_tool_activation_has_selection_prerequisite(const ViewportToolActivationRequest& request)
{
	const ViewportInteractionTool tool = sanitize_viewport_interaction_tool(request.requested_tool);
	if (tool == ViewportInteractionTool::cut) {
		return request.selected_cut_target_object_count > 0U;
	}
	if (tool == ViewportInteractionTool::mirror) {
		return request.selection_mode == SelectionMode::object && request.selected_object_count > 0U;
	}
	return true;
}

inline std::string_view viewport_tool_selection_prerequisite_message(ViewportInteractionTool tool)
{
	if (tool == ViewportInteractionTool::cut) {
		return "Select one or more mesh objects to cut.";
	}
	if (tool == ViewportInteractionTool::mirror) {
		return "Select one or more mesh objects to mirror.";
	}
	return "Select the required mesh objects before activating this tool.";
}

inline ViewportToolActivationPlan plan_viewport_tool_activation(const ViewportToolActivationRequest& request)
{
	ViewportToolActivationPlan plan;
	plan.active_tool = sanitize_viewport_interaction_tool(request.requested_tool);
	plan.selection_mode = request.selection_mode;
	plan.active_tool_changed = plan.active_tool != request.current_tool;
	if (viewport_tool_requires_selected_objects(plan.active_tool) &&
		!viewport_tool_activation_has_selection_prerequisite(request)) {
		plan.active_tool = sanitize_viewport_interaction_tool(request.current_tool);
		plan.selection_mode = request.selection_mode;
		plan.active_tool_changed = false;
		plan.cancel_transform_preview = false;
		plan.reset_transform_drag = false;
		plan.reset_insert_edge_loop_tool = false;
		plan.reset_knife_tool = false;
		plan.reset_cut_tool = false;
		plan.reset_mirror_tool = false;
		plan.reset_poly_tool = false;
		plan.reset_box_tool = false;
		plan.reset_box_grid_size = false;
		plan.clear_gizmo_hover = false;
		plan.activation_blocked = true;
		plan.blocked_message = viewport_tool_selection_prerequisite_message(request.requested_tool);
		return plan;
	}
	plan.cancel_transform_preview = request.transform_drag_active || request.transform_preview_active;
	plan.reset_transform_drag = !plan.cancel_transform_preview;

	if (plan.active_tool == ViewportInteractionTool::insert_edge_loop) {
		plan.selection_mode = SelectionMode::edge;
		plan.insert_edge_loop_tool_active = true;
		plan.reset_insert_edge_loop_tool = plan.active_tool_changed;
		plan.promote_hover_object_to_candidate = request.hover_has_object && !request.hover_has_candidate_object;
	} else if (plan.active_tool == ViewportInteractionTool::cut) {
		plan.cut_tool_active = true;
		plan.reset_cut_tool = plan.active_tool_changed;
	} else if (plan.active_tool == ViewportInteractionTool::mirror) {
		plan.selection_mode = SelectionMode::object;
		plan.mirror_tool_active = true;
		plan.reset_mirror_tool = plan.active_tool_changed;
	} else if (plan.active_tool == ViewportInteractionTool::poly) {
		plan.poly_tool_active = true;
		plan.reset_poly_tool = plan.active_tool_changed;
	} else if (plan.active_tool == ViewportInteractionTool::knife) {
		plan.knife_tool_active = true;
		plan.reset_knife_tool = plan.active_tool_changed;
	}

	plan.selection_mode_changed = plan.selection_mode != request.selection_mode;
	plan.reset_box_tool = plan.active_tool != ViewportInteractionTool::box;
	plan.reset_box_grid_size = plan.reset_box_tool;
	return plan;
}

} // namespace quader_editor_core

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/interactions/box_tool.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/modeling/runtime/tool_runtime_policy.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader_editor_core {

namespace {

CommandResult settings_mismatch_result()
{
	return { false, false, "Runtime command settings do not match the command id." };
}

CommandResult blocked_tool_activation_result(
		const EditorRuntimeToolActivationResult& activation,
		ViewportInteractionTool requested_tool)
{
	return {
		false,
		false,
		activation.plan.blocked_message.empty() ?
			std::string(viewport_tool_selection_prerequisite_message(requested_tool)) :
			std::string(activation.plan.blocked_message),
	};
}

CommandResult missing_cut_selection_result()
{
	return { false, false, "Select one or more mesh objects to cut." };
}

CommandResult missing_mirror_selection_result()
{
	return { false, false, "Select one or more mesh objects to mirror." };
}

bool has_object_selection_for_drawn_plane_tool(const EditorSession& session)
{
  return session.selection_mode() == SelectionMode::Object &&
         !session.selected_object_ids().empty();
}

CommandResult clear_hover(EditorRuntime& runtime)
{
	const bool was_hovering = runtime.status().hover_active;
	runtime.clear_hover();
	return { true, was_hovering, "" };
}

CommandResult set_active_tool(
		const EditorSession& session,
		EditorRuntime& runtime,
		SetActiveToolRuntimeCommandSettings settings)
{
	const ViewportInteractionTool active_tool_before = runtime.active_tool();
	const EditorRuntimeToolActivationResult activation = runtime.set_active_tool(session, settings.tool);
	if (activation.plan.activation_blocked) {
		return blocked_tool_activation_result(activation, settings.tool);
	}
	const bool active_tool_changed = runtime.active_tool() != active_tool_before;
	return { true, active_tool_changed || activation.transform_preview_cancelled, "" };
}

bool box_runtime_status_changed(const EditorRuntimeStatus& before, const EditorRuntimeStatus& after)
{
	return before.box_active != after.box_active ||
		before.box_grid_size != after.box_grid_size ||
		before.box_grid_at_default != after.box_grid_at_default ||
		before.hover_active != after.hover_active;
}

std::optional<PickHitRecord> resolved_or_runtime_surface_hit(
		EditorSession& session,
		const quader_poly::Ray& ray,
		const std::optional<PickHitRecord>& resolved_surface_hit,
		bool surface_hit_resolved)
{
	if (surface_hit_resolved) {
		return resolved_surface_hit;
	}
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::Face;
	return session.pick_nearest({ ray, options });
}

BoxToolStateResult set_box_construction_plane_from_ray(
		EditorSession& session,
		EditorRuntime& runtime,
		const BoxRayRuntimeCommandSettings& settings)
{
	const std::optional<PickHitRecord> hit = resolved_or_runtime_surface_hit(
		session, settings.ray, settings.resolved_surface_hit,
		settings.surface_hit_resolved);
	if (hit.has_value() && quader::length_squared(hit->local_normal) > 0.000001F) {
		return runtime.set_box_construction_plane(hit->local_position, hit->local_normal);
	}
	return runtime.set_box_construction_plane({ 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
}

CommandResult box_state_result_to_command(
		const EditorRuntimeStatus& before,
		const EditorRuntime& runtime,
		const BoxToolStateResult& result,
		std::string_view fallback)
{
	if (!result.ok) {
		return { false, false, result.message.empty() ? std::string(fallback) : result.message };
	}
	return { true, true, "" };
}

void clear_selection_for_box_placement(EditorSession& session, EditorRuntime& runtime)
{
	if (!session.has_selection()) {
		return;
	}
	session.clear_selection();
	runtime.clear_selection_presentation_state();
}

CommandResult update_box_hover(
		EditorSession& session,
		EditorRuntime& runtime,
		BoxRayRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	const BoxToolState& box = runtime.box_state();
        if (box.drag_state != BoxDragState::Idle) {
          return {true, false, ""};
        }

	runtime.set_box_grid_size(settings.grid_size);
	const BoxToolStateResult plane_result =
		set_box_construction_plane_from_ray(session, runtime, settings);
	if (!plane_result.ok) {
		return box_state_result_to_command(before, runtime, plane_result, "Box hover failed.");
	}

	const BoxConstructionPointResult point = runtime.box_construction_point_from_ray(settings.ray);
	if (!point.ok) {
		runtime.reset_box_state();
		return { true, box_runtime_status_changed(before, runtime.status()), "" };
	}

	const BoxToolStateResult result = runtime.update_box_hover_cell(point.position, settings.snap_enabled);
	return box_state_result_to_command(before, runtime, result, "Box hover failed.");
}

CommandResult begin_box_footprint(
		EditorSession& session,
		EditorRuntime& runtime,
		BoxRayRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	const std::uint64_t selection_revision_before = session.state().selection_revision;
	runtime.set_box_grid_size(settings.grid_size);
	const BoxToolStateResult plane_result =
		set_box_construction_plane_from_ray(session, runtime, settings);
	if (!plane_result.ok) {
		return box_state_result_to_command(before, runtime, plane_result, "Box footprint failed.");
	}

	const BoxConstructionPointResult point = runtime.box_construction_point_from_ray(settings.ray);
	if (!point.ok) {
		return { false, false, point.message.empty() ? "Box needs a ground-plane hit." : point.message };
	}

	const BoxToolStateResult result = runtime.begin_box_footprint(point.position, point.position, settings.snap_enabled);
	if (!result.ok) {
		return box_state_result_to_command(before, runtime, result, "Box footprint failed.");
	}
	clear_selection_for_box_placement(session, runtime);
	return {
		true,
		box_runtime_status_changed(before, runtime.status()) ||
			session.state().selection_revision != selection_revision_before,
		"" };
}

CommandResult update_box_footprint(
		EditorSession& session,
		EditorRuntime& runtime,
		BoxRayRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.set_box_grid_size(settings.grid_size);
	const BoxConstructionPointResult point = runtime.box_construction_point_from_ray(settings.ray);
	if (!point.ok) {
		return { false, false, point.message.empty() ? "Box needs a construction-plane hit." : point.message };
	}
	const BoxToolStateResult result = runtime.update_box_footprint(point.position, settings.snap_enabled);
	return box_state_result_to_command(before, runtime, result, "Box footprint update failed.");
}

CommandResult begin_box_height(
		EditorRuntime& runtime,
		BoxHeightRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.set_box_grid_size(settings.grid_size);
	const double axis_distance = runtime.box_height_distance_for_ray(settings.ray);
	const BoxToolStateResult result =
		runtime.begin_box_height(settings.screen_position, axis_distance, std::isfinite(axis_distance), settings.snap_enabled);
	return box_state_result_to_command(before, runtime, result, "Box height failed.");
}

CommandResult update_box_height(
		EditorRuntime& runtime,
		BoxHeightRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.set_box_grid_size(settings.grid_size);
	const double axis_distance = runtime.box_height_distance_for_ray(settings.ray);
	const BoxToolStateResult result =
		runtime.update_box_height_preview(axis_distance, std::isfinite(axis_distance), settings.screen_position, settings.snap_enabled);
	return box_state_result_to_command(before, runtime, result, "Box height update failed.");
}

CommandResult cancel_box(EditorRuntime& runtime)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.reset_box_state();
	return { true, box_runtime_status_changed(before, runtime.status()), "" };
}

RectangleSelectionState rectangle_selection_for_cut(const EditorSession& session)
{
	RectangleSelectionState selection;
	selection.mode = session.selection_mode();
	selection.active_object_id = session.active_object_id();
	selection.selected_object_ids = session.selected_object_ids();
	return selection;
}

bool cut_runtime_status_changed(const EditorRuntimeStatus& before, const EditorRuntimeStatus& after)
{
	return before.active_tool != after.active_tool ||
		before.hover_active != after.hover_active ||
		before.gizmo_hover_axis != after.gizmo_hover_axis ||
		before.cut_active != after.cut_active ||
		before.cut_can_preview != after.cut_can_preview ||
		before.cut_preview_valid != after.cut_preview_valid ||
		before.cut_point_count != after.cut_point_count ||
		before.transform_drag_active != after.transform_drag_active;
}

CommandResult update_insert_edge_loop_hover(
		EditorSession& session,
		EditorRuntime& runtime,
		InsertEdgeLoopPickRuntimeCommandSettings settings)
{
  const EditorRuntimeToolActivationResult activation =
      runtime.set_active_tool(session, ViewportInteractionTool::InsertEdgeLoop);
  if (runtime.insert_edge_loop_state().dragging) {
    return {true, activation.applied || activation.transform_preview_cancelled,
            ""};
  }
	const bool preview_changed = settings.resolved_hit.has_value()
		? runtime.set_insert_edge_loop_preview_from_hit(
			session, *settings.resolved_hit, settings.resolved_factor, false)
		: runtime.set_insert_edge_loop_preview_from_pick(
			session, settings.ray, settings.grid_size, settings.vertex_radius,
			settings.edge_radius, settings.snap_enabled, false,
			settings.target_object_id);
	return { true, activation.applied || activation.transform_preview_cancelled || preview_changed, "" };
}

CommandResult begin_insert_edge_loop_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		InsertEdgeLoopPickRuntimeCommandSettings settings)
{
  const EditorRuntimeToolActivationResult activation =
      runtime.set_active_tool(session, ViewportInteractionTool::InsertEdgeLoop);
  const bool valid = settings.resolved_hit.has_value()
      ? runtime.set_insert_edge_loop_preview_from_hit(
            session, *settings.resolved_hit, settings.resolved_factor, true)
      : runtime.set_insert_edge_loop_preview_from_pick(
            session, settings.ray, settings.grid_size, settings.vertex_radius,
            settings.edge_radius, settings.snap_enabled, true,
            settings.target_object_id);
  const bool dragging_before = runtime.status().insert_edge_loop_dragging;
  runtime.set_insert_edge_loop_dragging(valid);
  runtime.clear_hover();
  runtime.clear_gizmo_hover();
  runtime.clear_transform_drag_state();
  return {true,
          activation.applied || activation.transform_preview_cancelled ||
              valid || dragging_before != valid,
          ""};
}

CommandResult update_insert_edge_loop_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		InsertEdgeLoopPickRuntimeCommandSettings settings)
{
	if (!runtime.insert_edge_loop_state().dragging) {
		return { true, false, "" };
	}
	const bool changed = settings.resolved_factor_from_world
		? runtime.update_insert_edge_loop_drag_factor_from_world(
			session, settings.resolved_factor)
		: runtime.update_insert_edge_loop_drag_factor(
			session, settings.ray, settings.grid_size,
			settings.snap_enabled);
	return { true, changed, "" };
}

CommandResult cancel_insert_edge_loop(EditorRuntime& runtime)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.reset_insert_edge_loop_state(true);
	const EditorRuntimeStatus after = runtime.status();
	const bool changed =
		before.insert_edge_loop_active != after.insert_edge_loop_active ||
		before.insert_edge_loop_dragging != after.insert_edge_loop_dragging ||
		before.insert_edge_loop_has_preview != after.insert_edge_loop_has_preview;
	return { true, changed, "" };
}

CommandResult activate_cut(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	runtime.reset_insert_edge_loop_state();
	runtime.reset_knife_state();
	runtime.reset_cut_state();
	runtime.reset_mirror_state();
	runtime.reset_poly_state();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Cut);
        runtime.clear_hover();
	runtime.clear_gizmo_hover();
	runtime.clear_transform_drag_state();
	const CutToolResult refresh =
		runtime.refresh_cut_preview_from_selection(rectangle_selection_for_cut(session), session.objects());
	if (!refresh.ok) {
		return { false, false, refresh.message.empty() ? "Could not activate cut." : refresh.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			refresh.changed ||
			cut_runtime_status_changed(before, after),
		"" };
}

CommandResult update_cut_preview(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Cut);
        runtime.reset_insert_edge_loop_state();
	runtime.reset_knife_state();
	runtime.reset_mirror_state();
	runtime.reset_poly_state();
	const CutToolResult refresh =
		runtime.refresh_cut_preview_from_selection(rectangle_selection_for_cut(session), session.objects());
	if (!refresh.ok) {
		return { false, false, refresh.message.empty() ? "Could not update cut preview." : refresh.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			refresh.changed ||
			cut_runtime_status_changed(before, after),
		"" };
}

CommandResult set_cut_mode(EditorSession& session, EditorRuntime& runtime, SetCutModeRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Cut);
        runtime.reset_poly_state();
	runtime.reset_mirror_state();
	const CutToolResult mode_result = runtime.set_cut_mode(cut_mode_from_id(settings.mode));
	const CutToolResult refresh =
		runtime.refresh_cut_preview_from_selection(rectangle_selection_for_cut(session), session.objects());
	if (!mode_result.ok) {
		return { false, false, mode_result.message.empty() ? "Could not set cut mode." : mode_result.message };
	}
	if (!refresh.ok) {
		return { false, false, refresh.message.empty() ? "Could not refresh cut preview." : refresh.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			mode_result.changed ||
			refresh.changed ||
			cut_runtime_status_changed(before, after),
		"" };
}

CommandResult cancel_cut(EditorSession& session, EditorRuntime& runtime)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.reset_cut_state();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Select);
        runtime.clear_hover();
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			cut_runtime_status_changed(before, after),
		"" };
}

CutRayUpdateInput cut_ray_input(CutRayRuntimeCommandSettings settings)
{
	return {
		settings.camera,
		settings.ray,
		settings.grid_size,
		settings.dragging,
	};
}

EditorRuntimeToolActivationResult activate_cut_interaction(
		const EditorSession& session,
		EditorRuntime& runtime)
{
  const EditorRuntimeToolActivationResult activation =
      runtime.set_active_tool(session, ViewportInteractionTool::Cut);
  if (activation.plan.activation_blocked) {
    return activation;
  }
	runtime.reset_insert_edge_loop_state();
	runtime.reset_knife_state();
	runtime.reset_mirror_state();
	runtime.reset_poly_state();
	return activation;
}

CommandResult update_cut_hover(
		const EditorSession& session,
		EditorRuntime& runtime,
		CutRayRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_cut_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(activation,
                                                ViewportInteractionTool::Cut);
        }
	const CutToolResult result =
		runtime.update_cut_hover_from_ray(
			rectangle_selection_for_cut(session),
			session.objects(),
			cut_ray_input(settings));
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Cut hover was rejected." : result.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			cut_runtime_status_changed(before, after),
		"" };
}

CommandResult click_cut(
		EditorSession& session,
		EditorRuntime& runtime,
		CutRayRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_cut_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(activation,
                                                ViewportInteractionTool::Cut);
        }
	CutRayUpdateInput input = cut_ray_input(settings);
	input.dragging = false;
	const CutToolResult result =
		runtime.click_cut_from_ray(
			rectangle_selection_for_cut(session),
			session.objects(),
			input);
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Cut point was rejected." : result.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			cut_runtime_status_changed(before, after),
		"" };
}

CommandResult begin_cut_point_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		CutPointDragRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_cut_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(activation,
                                                ViewportInteractionTool::Cut);
        }
	const CutToolResult result = runtime.begin_cut_point_drag(settings.point_index);
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Cut point drag was rejected." : result.message };
	}
	(void)runtime.refresh_cut_preview_from_selection(rectangle_selection_for_cut(session), session.objects());
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			cut_runtime_status_changed(before, after),
		"" };
}

CommandResult begin_cut_line_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		CutRayRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_cut_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(activation,
                                                ViewportInteractionTool::Cut);
        }
	CutRayUpdateInput input = cut_ray_input(settings);
	input.dragging = true;
	const CutToolResult result = runtime.begin_cut_line_drag(input);
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Cut line drag was rejected." : result.message };
	}
	(void)runtime.refresh_cut_preview_from_selection(rectangle_selection_for_cut(session), session.objects());
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			cut_runtime_status_changed(before, after),
		"" };
}

CommandResult end_cut_drag(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Cut);
        if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(activation,
                                                ViewportInteractionTool::Cut);
        }
	runtime.reset_poly_state();
	const CutToolResult result = runtime.end_cut_drag();
	(void)runtime.refresh_cut_preview_from_selection(rectangle_selection_for_cut(session), session.objects());
	const EditorRuntimeStatus after = runtime.status();
	return {
		result.ok,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			cut_runtime_status_changed(before, after),
		result.message };
}

RectangleSelectionState rectangle_selection_for_mirror(const EditorSession& session)
{
	RectangleSelectionState selection;
	selection.mode = session.selection_mode();
	selection.active_object_id = session.active_object_id();
	selection.selected_object_ids = session.selected_object_ids();
	return selection;
}

bool mirror_runtime_status_changed(const EditorRuntimeStatus& before, const EditorRuntimeStatus& after)
{
	return before.active_tool != after.active_tool ||
		before.hover_active != after.hover_active ||
		before.gizmo_hover_axis != after.gizmo_hover_axis ||
		before.mirror_active != after.mirror_active ||
		before.mirror_can_preview != after.mirror_can_preview ||
		before.mirror_preview_valid != after.mirror_preview_valid ||
		before.mirror_point_count != after.mirror_point_count ||
		before.transform_drag_active != after.transform_drag_active;
}

MirrorRayUpdateInput mirror_ray_input(MirrorRayRuntimeCommandSettings settings)
{
	return {
		settings.camera,
		settings.ray,
		settings.grid_size,
		settings.dragging,
	};
}

EditorRuntimeToolActivationResult activate_mirror_interaction(EditorSession& session, EditorRuntime& runtime)
{
  const EditorRuntimeToolActivationResult activation =
      runtime.set_active_tool(session, ViewportInteractionTool::Mirror);
  if (activation.plan.activation_blocked) {
    return activation;
  }
	runtime.reset_insert_edge_loop_state();
	runtime.reset_knife_state();
	runtime.reset_cut_state();
	runtime.reset_poly_state();
	return activation;
}

CommandResult activate_mirror(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	runtime.reset_insert_edge_loop_state();
	runtime.reset_knife_state();
	runtime.reset_cut_state();
	runtime.reset_mirror_state();
	runtime.reset_poly_state();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Mirror);
        runtime.clear_hover();
	runtime.clear_gizmo_hover();
	runtime.clear_transform_drag_state();
	const MirrorToolResult refresh =
		runtime.refresh_mirror_preview_from_selection(rectangle_selection_for_mirror(session), session.objects());
	if (!refresh.ok) {
		return { false, false, refresh.message.empty() ? "Could not activate mirror." : refresh.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			refresh.changed ||
			mirror_runtime_status_changed(before, after),
		"" };
}

CommandResult update_mirror_preview(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Mirror);
        runtime.reset_insert_edge_loop_state();
	runtime.reset_knife_state();
	runtime.reset_cut_state();
	runtime.reset_poly_state();
	const MirrorToolResult refresh =
		runtime.refresh_mirror_preview_from_selection(rectangle_selection_for_mirror(session), session.objects());
	if (!refresh.ok) {
		return { false, false, refresh.message.empty() ? "Could not update mirror preview." : refresh.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			refresh.changed ||
			mirror_runtime_status_changed(before, after),
		"" };
}

CommandResult cancel_mirror(EditorSession& session, EditorRuntime& runtime)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.reset_mirror_state();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Select);
        runtime.clear_hover();
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			mirror_runtime_status_changed(before, after),
		"" };
}

CommandResult update_mirror_hover(
		EditorSession& session,
		EditorRuntime& runtime,
		MirrorRayRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_mirror_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(
              activation, ViewportInteractionTool::Mirror);
        }
	const MirrorToolResult result =
		runtime.update_mirror_hover_from_ray(
			rectangle_selection_for_mirror(session),
			session.objects(),
			mirror_ray_input(settings));
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Mirror hover was rejected." : result.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			mirror_runtime_status_changed(before, after),
		"" };
}

CommandResult click_mirror(
		EditorSession& session,
		EditorRuntime& runtime,
		MirrorRayRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_mirror_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(
              activation, ViewportInteractionTool::Mirror);
        }
	MirrorRayUpdateInput input = mirror_ray_input(settings);
	input.dragging = false;
	const MirrorToolResult result =
		runtime.click_mirror_from_ray(
			rectangle_selection_for_mirror(session),
			session.objects(),
			input);
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Mirror point was rejected." : result.message };
	}
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			mirror_runtime_status_changed(before, after),
		"" };
}

CommandResult begin_mirror_point_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		MirrorPointDragRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_mirror_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(
              activation, ViewportInteractionTool::Mirror);
        }
	const MirrorToolResult result = runtime.begin_mirror_point_drag(settings.point_index);
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Mirror point drag was rejected." : result.message };
	}
	(void)runtime.refresh_mirror_preview_from_selection(rectangle_selection_for_mirror(session), session.objects());
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			mirror_runtime_status_changed(before, after),
		"" };
}

CommandResult begin_mirror_line_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		MirrorRayRuntimeCommandSettings settings)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_mirror_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(
              activation, ViewportInteractionTool::Mirror);
        }
	MirrorRayUpdateInput input = mirror_ray_input(settings);
	input.dragging = true;
	const MirrorToolResult result = runtime.begin_mirror_line_drag(input);
	if (!result.ok) {
		return { false, false, result.message.empty() ? "Mirror line drag was rejected." : result.message };
	}
	(void)runtime.refresh_mirror_preview_from_selection(rectangle_selection_for_mirror(session), session.objects());
	const EditorRuntimeStatus after = runtime.status();
	return {
		true,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			mirror_runtime_status_changed(before, after),
		"" };
}

CommandResult end_mirror_drag(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
        const EditorRuntimeToolActivationResult activation =
            runtime.set_active_tool(session, ViewportInteractionTool::Mirror);
        if (activation.plan.activation_blocked) {
          return blocked_tool_activation_result(
              activation, ViewportInteractionTool::Mirror);
        }
	runtime.reset_cut_state();
	runtime.reset_poly_state();
	const MirrorToolResult result = runtime.end_mirror_drag();
	(void)runtime.refresh_mirror_preview_from_selection(rectangle_selection_for_mirror(session), session.objects());
	const EditorRuntimeStatus after = runtime.status();
	return {
		result.ok,
		activation.applied ||
			activation.transform_preview_cancelled ||
			result.changed ||
			mirror_runtime_status_changed(before, after),
		result.message };
}

CommandResult update_poly_hover(
    EditorSession &session, EditorRuntime &runtime,
    PolyHoverRuntimeCommandSettings settings) {
  quader::QVec3 position;
  quader::QVec3 plane_normal;
  const PolyToolState &state = runtime.poly_state();
  if (state.closed) {
    return {true, false, ""};
  }
  if (!state.points.empty()) {
    const BoxConstructionPlane plane = normalize_box_construction_plane(
        state.points.front(), state.face_normal);
    const BoxConstructionPointResult point =
        box_construction_point_from_ray(plane, settings.ray);
    if (!point.ok) {
      return {true, false, point.message};
    }
    position = point.position;
    plane_normal = plane.normal;
  } else {
    const std::optional<PickHitRecord> surface_hit =
        resolved_or_runtime_surface_hit(
            session, settings.ray, settings.resolved_surface_hit,
            settings.surface_hit_resolved);
    if (surface_hit.has_value() &&
        quader::length_squared(surface_hit->local_normal) > 0.000001F) {
      position = surface_hit->local_position;
      plane_normal = surface_hit->local_normal;
    } else {
      const CutGridPointResult grid_point = viewport_grid_point_from_ray(
          settings.camera, settings.ray, settings.grid_size, false);
      if (!grid_point.ok) {
        return {true, false, grid_point.message};
      }
      position = grid_point.position;
      plane_normal = grid_point.plane_normal;
    }
  }
  const PolyToolResult result = runtime.update_poly_hover(
      position, plane_normal, settings.constrain_axis, settings.grid_size,
      settings.snap_enabled);
  return {result.ok, result.changed, result.message};
}

CommandResult accept_poly_point(EditorRuntime &runtime) {
  const PolyToolResult result = runtime.accept_poly_current_point();
  return {result.ok, result.changed, result.message};
}

CommandResult cancel_poly(EditorRuntime &runtime) {
  const EditorRuntimeStatus before = runtime.status();
  runtime.reset_poly_state();
  runtime.clear_hover();
  const EditorRuntimeStatus after = runtime.status();
  const bool changed = before.poly_active != after.poly_active ||
                       before.poly_closed != after.poly_closed ||
                       before.poly_has_current != after.poly_has_current ||
                       before.poly_can_commit != after.poly_can_commit ||
                       before.poly_point_count != after.poly_point_count ||
                       before.hover_active != after.hover_active;
  return {true, changed, ""};
}

EditorRuntimeCommandResult runtime_command_result(EditorRuntime& runtime, CommandResult command)
{
	return { std::move(command), runtime.status() };
}

} // namespace

EditorRuntimeCommandResult execute_cut_hover_runtime_command(
		const EditorSession& session,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command)
{
	if (command.command_id != EditorRuntimeCommandId::UpdateCutHover) {
		return runtime_command_result(
			runtime, { false, false, "Command is not a Cut hover update." });
	}
	const auto* settings =
		std::get_if<CutRayRuntimeCommandSettings>(&command.settings);
	if (settings == nullptr) {
		return runtime_command_result(runtime, settings_mismatch_result());
	}
	return runtime_command_result(
		runtime, update_cut_hover(session, runtime, *settings));
}

EditorRuntimeCommandResult execute_runtime_command(
		EditorSession& session,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command)
{
	switch (command.command_id) {
	case EditorRuntimeCommandId::ClearHover:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime, clear_hover(runtime));
        case EditorRuntimeCommandId::SetActiveTool: {
          const auto *settings =
              std::get_if<SetActiveToolRuntimeCommandSettings>(
                  &command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, set_active_tool(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::UpdateInsertEdgeLoopHover: {
          const auto *settings =
              std::get_if<InsertEdgeLoopPickRuntimeCommandSettings>(
                  &command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime,
              update_insert_edge_loop_hover(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::BeginInsertEdgeLoopDrag: {
          const auto *settings =
              std::get_if<InsertEdgeLoopPickRuntimeCommandSettings>(
                  &command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime,
              begin_insert_edge_loop_drag(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::UpdateInsertEdgeLoopDrag: {
          const auto *settings =
              std::get_if<InsertEdgeLoopPickRuntimeCommandSettings>(
                  &command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime,
              update_insert_edge_loop_drag(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::CancelInsertEdgeLoop:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        cancel_insert_edge_loop(runtime));
        case EditorRuntimeCommandId::ActivateCut:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        activate_cut(session, runtime));
        case EditorRuntimeCommandId::UpdateCutPreview:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        update_cut_preview(session, runtime));
        case EditorRuntimeCommandId::SetCutMode: {
          const auto *settings =
              std::get_if<SetCutModeRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, set_cut_mode(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::CancelCut:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime, cancel_cut(session, runtime));
        case EditorRuntimeCommandId::UpdateCutHover: {
          return execute_cut_hover_runtime_command(session, runtime, command);
        }
        case EditorRuntimeCommandId::ClickCut: {
          const auto *settings =
              std::get_if<CutRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        click_cut(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::BeginCutPointDrag: {
          const auto *settings =
              std::get_if<CutPointDragRuntimeCommandSettings>(
                  &command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, begin_cut_point_drag(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::BeginCutLineDrag: {
          const auto *settings =
              std::get_if<CutRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, begin_cut_line_drag(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::EndCutDrag:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        end_cut_drag(session, runtime));
        case EditorRuntimeCommandId::ActivateMirror:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        activate_mirror(session, runtime));
        case EditorRuntimeCommandId::UpdateMirrorPreview:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, update_mirror_preview(session, runtime));
        case EditorRuntimeCommandId::CancelMirror:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        cancel_mirror(session, runtime));
        case EditorRuntimeCommandId::UpdateMirrorHover: {
          const auto *settings =
              std::get_if<MirrorRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, update_mirror_hover(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::ClickMirror: {
          const auto *settings =
              std::get_if<MirrorRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, click_mirror(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::BeginMirrorPointDrag: {
          const auto *settings =
              std::get_if<MirrorPointDragRuntimeCommandSettings>(
                  &command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, begin_mirror_point_drag(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::BeginMirrorLineDrag: {
          const auto *settings =
              std::get_if<MirrorRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, begin_mirror_line_drag(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::EndMirrorDrag:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        end_mirror_drag(session, runtime));
        case EditorRuntimeCommandId::UpdatePolyHover: {
          const auto *settings =
              std::get_if<PolyHoverRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, update_poly_hover(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::AcceptPolyPoint:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime, accept_poly_point(runtime));
        case EditorRuntimeCommandId::CancelPoly:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime, cancel_poly(runtime));
        case EditorRuntimeCommandId::UpdateBoxHover: {
          const auto *settings =
              std::get_if<BoxRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, update_box_hover(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::BeginBoxFootprint: {
          const auto *settings =
              std::get_if<BoxRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, begin_box_footprint(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::UpdateBoxFootprint: {
          const auto *settings =
              std::get_if<BoxRayRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(
              runtime, update_box_footprint(session, runtime, *settings));
        }
        case EditorRuntimeCommandId::BeginBoxHeight: {
          const auto *settings =
              std::get_if<BoxHeightRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        begin_box_height(runtime, *settings));
        }
        case EditorRuntimeCommandId::UpdateBoxHeight: {
          const auto *settings =
              std::get_if<BoxHeightRuntimeCommandSettings>(&command.settings);
          if (settings == nullptr) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime,
                                        update_box_height(runtime, *settings));
        }
        case EditorRuntimeCommandId::CancelBox:
          if (!std::holds_alternative<std::monostate>(command.settings)) {
            return runtime_command_result(runtime, settings_mismatch_result());
          }
          return runtime_command_result(runtime, cancel_box(runtime));
	}

	return runtime_command_result(runtime, { false, false, "Unknown runtime command." });
}

} // namespace quader_editor_core

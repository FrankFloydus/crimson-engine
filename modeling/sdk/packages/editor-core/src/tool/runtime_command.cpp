#include <quader_editor_core/runtime_command.h>
#include <quader_editor_core/tool/selection_edit.h>
#include <quader_editor_core/tool_runtime_policy.h>

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
	return session.selection_mode() == SelectionMode::object && !session.selected_object_ids().empty();
}

CommandResult copy_selected_meshes(EditorSession& session, EditorRuntime& runtime)
{
	const std::size_t copied_count = runtime.copy_selected_objects_to_clipboard(session);
	if (copied_count == 0) {
		return { false, false, "Select one or more meshes in object mode before copying." };
	}
	return { true, false, "" };
}

CommandResult clear_selection(EditorSession& session)
{
	const std::uint64_t selection_revision_before = session.state().selection_revision;
	session.clear_selection();
	return { true, session.state().selection_revision != selection_revision_before, "" };
}

CommandResult delete_selection(EditorSession& session)
{
	if (!session.has_selection()) {
		return { true, false, "" };
	}
	return session.apply_history_command([](EditorSession& editor_session) {
		return editor_session.delete_selected();
	});
}

CommandResult mark_clean(EditorSession& session)
{
	const bool was_dirty = session.dirty();
	session.mark_clean();
	return { true, was_dirty, "" };
}

CommandResult restore_undo(EditorSession& session, EditorRuntime& runtime)
{
	const bool transient_restored = runtime.discard_transient_state(session, true);
	const HistoryRestoreResult restored = session.undo();
	if (restored.applied) {
		(void)runtime.discard_transient_state(session, false);
	}
	return { true, transient_restored || restored.applied, "" };
}

CommandResult restore_redo(EditorSession& session, EditorRuntime& runtime)
{
	const bool transient_restored = runtime.discard_transient_state(session, true);
	const HistoryRestoreResult restored = session.redo();
	if (restored.applied) {
		(void)runtime.discard_transient_state(session, false);
	}
	return { true, transient_restored || restored.applied, "" };
}

CommandResult clear_hover(EditorRuntime& runtime)
{
	const bool was_hovering = runtime.status().hover_active;
	runtime.clear_hover();
	return { true, was_hovering, "" };
}

CommandResult set_selection_mode(EditorSession& session, SetSelectionModeRuntimeCommandSettings settings)
{
	const std::uint64_t selection_revision_before = session.state().selection_revision;
	session.set_selection_mode(sanitize_selection_mode(settings.selection_mode));
	return { true, session.state().selection_revision != selection_revision_before, "" };
}

CommandResult set_active_tool(
		EditorSession& session,
		EditorRuntime& runtime,
		SetActiveToolRuntimeCommandSettings settings)
{
	const ViewportInteractionTool active_tool_before = runtime.active_tool();
	const std::uint64_t selection_revision_before = session.state().selection_revision;
	const bool preview_active_before = session.preview_transaction_status().active;
	const EditorRuntimeToolActivationResult activation = runtime.set_active_tool(session, settings.tool);
	if (activation.plan.activation_blocked) {
		return blocked_tool_activation_result(activation, settings.tool);
	}
	const bool active_tool_changed = runtime.active_tool() != active_tool_before;
	const bool selection_changed = session.state().selection_revision != selection_revision_before;
	const bool preview_active_changed = session.preview_transaction_status().active != preview_active_before;
	return { true, active_tool_changed || selection_changed || preview_active_changed || activation.transform_preview_cancelled, "" };
}

CommandResult pick_select(EditorSession& session, PickSelectRuntimeCommandSettings settings)
{
	(void)settings.command_pressed;
	(void)settings.alt_pressed;
	const SelectionMode mode = session.selection_mode();
	SelectionEditOperation operation =
			selection_edit_operation_from_modifiers(settings.control_pressed, settings.shift_pressed);
	quader_poly::PickOptions pick_options =
		selection_pick_options_from_radii(settings.vertex_radius, settings.edge_radius);
	pick_options.has_hidden_material_slot = settings.has_hidden_material_slot;
	pick_options.hidden_material_slot = settings.hidden_material_slot;
	const bool paint_stroke = settings.click_count == 0;
	if (paint_stroke) {
		if (mode == SelectionMode::object) {
			return { true, false, "" };
		}
		if (operation == SelectionEditOperation::replace) {
			operation = SelectionEditOperation::add;
		}
		return { true, session.pick_select(operation, settings.ray, pick_options, false), "" };
	}
	const bool multi_click = settings.click_count > 1;
	if (multi_click && mode == SelectionMode::face) {
		return { true, session.pick_select_all_faces(operation, settings.ray, pick_options), "" };
	}
	if (multi_click && mode == SelectionMode::edge) {
		return { true, session.pick_select_edge_loop(operation, settings.ray, pick_options), "" };
	}
	if (multi_click && mode == SelectionMode::vertex) {
		return { true, session.pick_select_all_vertices(operation, settings.ray, pick_options), "" };
	}
	return { true, session.pick_select(operation, settings.ray, pick_options), "" };
}

bool box_runtime_status_changed(const EditorRuntimeStatus& before, const EditorRuntimeStatus& after)
{
	return before.box_active != after.box_active ||
		before.box_grid_size != after.box_grid_size ||
		before.box_grid_at_default != after.box_grid_at_default ||
		before.hover_active != after.hover_active;
}

std::optional<PickHitRecord> box_surface_hit(EditorSession& session, const quader_poly::Ray& ray)
{
	quader_poly::PickOptions options;
	options.mode = quader_poly::SelectionMode::face;
	return session.pick_nearest({ ray, options });
}

BoxToolStateResult set_box_construction_plane_from_ray(EditorSession& session, EditorRuntime& runtime, const quader_poly::Ray& ray)
{
	const std::optional<PickHitRecord> hit = box_surface_hit(session, ray);
	if (hit.has_value() && quader_poly::length_squared(hit->local_normal) > 0.000001F) {
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
	if (box.drag_state != BoxDragState::idle) {
		return { true, false, "" };
	}

	runtime.set_box_grid_size(settings.grid_size);
	const BoxToolStateResult plane_result = set_box_construction_plane_from_ray(session, runtime, settings.ray);
	if (!plane_result.ok) {
		return box_state_result_to_command(before, runtime, plane_result, "Box hover failed.");
	}

	const BoxConstructionPointResult point = runtime.box_construction_point_from_ray(settings.ray);
	if (!point.ok) {
		runtime.reset_box_state();
		return { true, box_runtime_status_changed(before, runtime.status()), "" };
	}

	const BoxToolStateResult result = runtime.update_box_hover_cell(point.position, true);
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
	const BoxToolStateResult plane_result = set_box_construction_plane_from_ray(session, runtime, settings.ray);
	if (!plane_result.ok) {
		return box_state_result_to_command(before, runtime, plane_result, "Box footprint failed.");
	}

	const BoxConstructionPointResult point = runtime.box_construction_point_from_ray(settings.ray);
	if (!point.ok) {
		return { false, false, point.message.empty() ? "Box needs a ground-plane hit." : point.message };
	}

	const BoxToolStateResult result = runtime.begin_box_footprint(point.position, point.position, true);
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
	const BoxToolStateResult result = runtime.update_box_footprint(point.position, true);
	return box_state_result_to_command(before, runtime, result, "Box footprint update failed.");
}

CommandResult begin_box_height(
		EditorRuntime& runtime,
		BoxHeightRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.set_box_grid_size(settings.grid_size);
	const double axis_distance = runtime.box_height_distance_for_ray(settings.ray);
	const BoxToolStateResult result = settings.has_camera ?
		runtime.begin_box_height(
			settings.screen_position,
			axis_distance,
			std::isfinite(axis_distance),
			settings.camera,
			true) :
		runtime.begin_box_height(
			settings.screen_position,
			axis_distance,
			std::isfinite(axis_distance),
			true);
	return box_state_result_to_command(before, runtime, result, "Box height failed.");
}

CommandResult update_box_height(
		EditorRuntime& runtime,
		BoxHeightRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	runtime.set_box_grid_size(settings.grid_size);
	const double axis_distance = runtime.box_height_distance_for_ray(settings.ray);
	const BoxToolStateResult result = settings.has_camera ?
		runtime.update_box_height_preview(
			axis_distance,
			std::isfinite(axis_distance),
			settings.screen_position,
			settings.camera,
			true) :
		runtime.update_box_height_preview(
			axis_distance,
			std::isfinite(axis_distance),
			settings.screen_position,
			true);
	return box_state_result_to_command(before, runtime, result, "Box height update failed.");
}

CommandResult commit_box(EditorSession& session, EditorRuntime& runtime)
{
	const EditorRuntimeStatus before = runtime.status();
	const BoxPreviewDescriptor preview = runtime.box_preview_descriptor(true);
	if (!preview.ok) {
		return { false, false, preview.message.empty() ? "Box preview failed." : preview.message };
	}
	if (preview.drag_state != BoxDragState::height) {
		return { false, false, "Box height must be set before commit." };
	}
	const std::vector<quader_poly::Vec3> corners = box_preview_corners(preview);
	if (corners.size() != 8U) {
		return { false, false, "Box preview has no volume corners." };
	}
	if (preview.bounds.empty) {
		return { false, false, "Box preview has no bounds." };
	}

	CommandResult result =
		session.apply_history_command([corners](EditorSession& editor_session) {
			return editor_session.create_box_from_corners(corners);
		});
	runtime.reset_box_state();
	runtime.clear_hover();
	if (!result.ok && result.message.empty()) {
		result.message = "Box creation failed.";
	}
	result.changed = result.changed || box_runtime_status_changed(before, runtime.status());
	return result;
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
		runtime.set_active_tool(session, ViewportInteractionTool::insert_edge_loop);
	if (runtime.insert_edge_loop_state().dragging) {
		return { true, activation.applied || activation.transform_preview_cancelled, "" };
	}
	const bool preview_changed = runtime.set_insert_edge_loop_preview_from_pick(
			session,
			settings.ray,
			settings.grid_size,
			settings.vertex_radius,
			settings.edge_radius,
			settings.snap_enabled,
			false);
	return { true, activation.applied || activation.transform_preview_cancelled || preview_changed, "" };
}

CommandResult begin_insert_edge_loop_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		InsertEdgeLoopPickRuntimeCommandSettings settings)
{
	const EditorRuntimeToolActivationResult activation =
		runtime.set_active_tool(session, ViewportInteractionTool::insert_edge_loop);
	const bool valid = runtime.set_insert_edge_loop_preview_from_pick(
			session,
			settings.ray,
			settings.grid_size,
			settings.vertex_radius,
			settings.edge_radius,
			settings.snap_enabled,
			true);
	const bool dragging_before = runtime.status().insert_edge_loop_dragging;
	runtime.set_insert_edge_loop_dragging(valid);
	runtime.clear_hover();
	runtime.clear_gizmo_hover();
	runtime.clear_transform_drag_state();
	return { true, activation.applied || activation.transform_preview_cancelled || valid || dragging_before != valid, "" };
}

CommandResult update_insert_edge_loop_drag(
		EditorSession& session,
		EditorRuntime& runtime,
		InsertEdgeLoopPickRuntimeCommandSettings settings)
{
	if (!runtime.insert_edge_loop_state().dragging) {
		return { true, false, "" };
	}
	const bool changed = runtime.update_insert_edge_loop_drag_factor(
			session,
			settings.ray,
			settings.grid_size,
			settings.snap_enabled);
	return { true, changed, "" };
}

CommandResult commit_insert_edge_loop_drag(EditorSession& session, EditorRuntime& runtime)
{
	const InsertEdgeLoopToolState& edge_loop = runtime.insert_edge_loop_state();
	if (!edge_loop.dragging || edge_loop.preview_lines.empty()) {
		return { false, false, "Select an edge to insert an edge loop." };
	}

	const int object_id = edge_loop.object_id;
	const quader_poly::Edge edge = edge_loop.edge;
	const float factor = edge_loop.factor;
	CommandResult result =
		session.apply_history_command([object_id, edge, factor](EditorSession& editor_session) {
			return editor_session.insert_edge_loop(object_id, edge, factor);
		});
	if (!result.ok && result.message.empty()) {
		result.message = "Could not insert edge loop.";
	}

	runtime.reset_insert_edge_loop_state();
	runtime.set_insert_edge_loop_active(true);
	(void)runtime.set_active_tool(session, ViewportInteractionTool::insert_edge_loop);
	if (result.ok) {
		runtime.clear_hover();
		runtime.clear_gizmo_hover();
		runtime.clear_transform_drag_state();
	}
	return result;
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
		runtime.set_active_tool(session, ViewportInteractionTool::cut);
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
		runtime.set_active_tool(session, ViewportInteractionTool::cut);
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
		runtime.set_active_tool(session, ViewportInteractionTool::cut);
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
		runtime.set_active_tool(session, ViewportInteractionTool::select);
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

EditorRuntimeToolActivationResult activate_cut_interaction(EditorSession& session, EditorRuntime& runtime)
{
	const EditorRuntimeToolActivationResult activation =
		runtime.set_active_tool(session, ViewportInteractionTool::cut);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::cut);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::cut);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::cut);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::cut);
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
		runtime.set_active_tool(session, ViewportInteractionTool::cut);
	if (activation.plan.activation_blocked) {
		return blocked_tool_activation_result(activation, ViewportInteractionTool::cut);
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

CommandResult finalize_cut(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_cut_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_cut_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
		return blocked_tool_activation_result(activation, ViewportInteractionTool::cut);
	}
	(void)runtime.refresh_cut_preview_from_selection(rectangle_selection_for_cut(session), session.objects());
	CommandResult result = runtime.commit_cut_tool(session);
	if (!result.ok) {
		return {
			false,
			false,
			result.message.empty() ? "Could not apply the cut." : result.message };
	}
	const EditorRuntimeToolActivationResult select_activation =
		runtime.set_active_tool(session, ViewportInteractionTool::select);
	runtime.clear_hover();
	runtime.clear_cut_hover();
	const EditorRuntimeStatus after = runtime.status();
	result.changed = result.changed ||
		select_activation.applied ||
		select_activation.transform_preview_cancelled ||
		cut_runtime_status_changed(before, after);
	return result;
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
		runtime.set_active_tool(session, ViewportInteractionTool::mirror);
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
		runtime.set_active_tool(session, ViewportInteractionTool::mirror);
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
		runtime.set_active_tool(session, ViewportInteractionTool::mirror);
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
		runtime.set_active_tool(session, ViewportInteractionTool::select);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::mirror);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::mirror);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::mirror);
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
		return blocked_tool_activation_result(activation, ViewportInteractionTool::mirror);
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
		runtime.set_active_tool(session, ViewportInteractionTool::mirror);
	if (activation.plan.activation_blocked) {
		return blocked_tool_activation_result(activation, ViewportInteractionTool::mirror);
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

CommandResult finalize_mirror(EditorSession& session, EditorRuntime& runtime)
{
	if (!has_object_selection_for_drawn_plane_tool(session)) {
		return missing_mirror_selection_result();
	}

	const EditorRuntimeStatus before = runtime.status();
	const EditorRuntimeToolActivationResult activation = activate_mirror_interaction(session, runtime);
	if (activation.plan.activation_blocked) {
		return blocked_tool_activation_result(activation, ViewportInteractionTool::mirror);
	}
	(void)runtime.refresh_mirror_preview_from_selection(rectangle_selection_for_mirror(session), session.objects());
	CommandResult result = runtime.commit_mirror_tool(session);
	if (!result.ok) {
		return {
			false,
			false,
			result.message.empty() ? "Could not apply the mirror." : result.message };
	}
	const EditorRuntimeToolActivationResult select_activation =
		runtime.set_active_tool(session, ViewportInteractionTool::select);
	runtime.clear_hover();
	runtime.clear_mirror_hover();
	const EditorRuntimeStatus after = runtime.status();
	result.changed = result.changed ||
		select_activation.applied ||
		select_activation.transform_preview_cancelled ||
		mirror_runtime_status_changed(before, after);
	return result;
}

bool is_world_mirror_axis(TransformAxis axis)
{
	return axis == TransformAxis::x ||
		axis == TransformAxis::y ||
		axis == TransformAxis::z;
}

bool is_view_flip_orientation(ViewportFlipOrientation orientation)
{
	return orientation == ViewportFlipOrientation::horizontal ||
		orientation == ViewportFlipOrientation::vertical;
}

CommandResult mirror_axis(
		EditorSession& session,
		EditorRuntime& runtime,
		MirrorAxisRuntimeCommandSettings settings)
{
	if (!is_world_mirror_axis(settings.axis)) {
		return { false, false, "Mirror axis must be X, Y, or Z." };
	}
	if (session.selection_mode() != SelectionMode::object || session.selected_object_ids().empty()) {
		return { false, false, "Select one or more mesh objects to mirror." };
	}

	const bool transient_changed = runtime.discard_transient_state(session, true);
	CommandResult result = session.apply_history_command([settings](EditorSession& editor_session) {
		return editor_session.mirror_selected_objects_across_axis(settings.axis);
	});
	if (!result.ok) {
		return result;
	}
	result.changed = result.changed || transient_changed;
	return result;
}

CommandResult flip_view(
		EditorSession& session,
		EditorRuntime& runtime,
		ViewFlipRuntimeCommandSettings settings)
{
	if (!is_view_flip_orientation(settings.orientation)) {
		return { false, false, "Flip orientation must be horizontal or vertical." };
	}
	if (session.selection_mode() != SelectionMode::object || session.selected_object_ids().empty()) {
		return { false, false, "Select one or more mesh objects to flip." };
	}

	const bool transient_changed = runtime.discard_transient_state(session, true);
	CommandResult result = session.apply_history_command([settings](EditorSession& editor_session) {
		return editor_session.flip_selected_objects_by_orientation(settings.orientation);
	});
	if (!result.ok) {
		return result;
	}
	result.changed = result.changed || transient_changed;
	return result;
}

std::optional<quader_poly::Vec3> paste_target_from_settings(PasteCopiedMeshesRuntimeCommandSettings settings)
{
	if (settings.target_center.valid) {
		return settings.target_center.value;
	}
	return std::nullopt;
}

CommandResult paste_copied_meshes(
		EditorSession& session,
		EditorRuntime& runtime,
		PasteCopiedMeshesRuntimeCommandSettings settings)
{
	return session.apply_history_command([&runtime, settings](EditorSession& editor_session) {
		return runtime.paste_clipboard(editor_session, paste_target_from_settings(settings));
	});
}

CommandResult create_box_from_bounds(
		EditorSession& session,
		EditorRuntime& runtime,
		CreateBoxFromBoundsRuntimeCommandSettings settings)
{
	const EditorRuntimeStatus before = runtime.status();
	CommandResult result =
		session.apply_history_command([settings](EditorSession& editor_session) {
			return editor_session.create_box_from_bounds(settings.min_bounds, settings.max_bounds);
		});
	if (!result.ok) {
		if (result.message.empty()) {
			result.message = "Box creation failed.";
		}
		return result;
	}

	runtime.clear_hover();
	runtime.clear_gizmo_hover();
	runtime.clear_transform_drag_state();
	result.changed = result.changed || before.hover_active ||
		before.gizmo_hover_axis != TransformAxis::none ||
		before.transform_drag_active;
	return result;
}

EditorRuntimeCommandResult runtime_command_result(EditorRuntime& runtime, CommandResult command)
{
	return { std::move(command), runtime.status() };
}

} // namespace

EditorRuntimeCommandResult execute_runtime_command(
		EditorSession& session,
		EditorRuntime& runtime,
		const EditorRuntimeCommand& command)
{
	switch (command.command_id) {
	case EditorRuntimeCommandId::copy_selected_meshes:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, copy_selected_meshes(session, runtime));
	case EditorRuntimeCommandId::paste_copied_meshes: {
		const auto *settings = std::get_if<PasteCopiedMeshesRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, paste_copied_meshes(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::clear_selection:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, clear_selection(session));
	case EditorRuntimeCommandId::select_all:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, session.select_all());
	case EditorRuntimeCommandId::invert_selection:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, session.invert_selection());
	case EditorRuntimeCommandId::delete_selection:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, delete_selection(session));
	case EditorRuntimeCommandId::mark_clean:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, mark_clean(session));
	case EditorRuntimeCommandId::undo:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, restore_undo(session, runtime));
	case EditorRuntimeCommandId::redo:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, restore_redo(session, runtime));
	case EditorRuntimeCommandId::clear_hover:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, clear_hover(runtime));
	case EditorRuntimeCommandId::set_selection_mode: {
		const auto *settings = std::get_if<SetSelectionModeRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, set_selection_mode(session, *settings));
	}
	case EditorRuntimeCommandId::set_active_tool: {
		const auto *settings = std::get_if<SetActiveToolRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, set_active_tool(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::pick_select: {
		const auto *settings = std::get_if<PickSelectRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, pick_select(session, *settings));
	}
	case EditorRuntimeCommandId::update_insert_edge_loop_hover: {
		const auto *settings = std::get_if<InsertEdgeLoopPickRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_insert_edge_loop_hover(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::begin_insert_edge_loop_drag: {
		const auto *settings = std::get_if<InsertEdgeLoopPickRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, begin_insert_edge_loop_drag(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::update_insert_edge_loop_drag: {
		const auto *settings = std::get_if<InsertEdgeLoopPickRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_insert_edge_loop_drag(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::commit_insert_edge_loop_drag:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, commit_insert_edge_loop_drag(session, runtime));
	case EditorRuntimeCommandId::cancel_insert_edge_loop:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, cancel_insert_edge_loop(runtime));
	case EditorRuntimeCommandId::activate_cut:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, activate_cut(session, runtime));
	case EditorRuntimeCommandId::update_cut_preview:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_cut_preview(session, runtime));
	case EditorRuntimeCommandId::set_cut_mode: {
		const auto *settings = std::get_if<SetCutModeRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, set_cut_mode(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::cancel_cut:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, cancel_cut(session, runtime));
	case EditorRuntimeCommandId::update_cut_hover: {
		const auto *settings = std::get_if<CutRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_cut_hover(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::click_cut: {
		const auto *settings = std::get_if<CutRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, click_cut(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::begin_cut_point_drag: {
		const auto *settings = std::get_if<CutPointDragRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, begin_cut_point_drag(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::begin_cut_line_drag: {
		const auto *settings = std::get_if<CutRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, begin_cut_line_drag(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::end_cut_drag:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, end_cut_drag(session, runtime));
	case EditorRuntimeCommandId::finalize_cut:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, finalize_cut(session, runtime));
	case EditorRuntimeCommandId::activate_mirror:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, activate_mirror(session, runtime));
	case EditorRuntimeCommandId::update_mirror_preview:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_mirror_preview(session, runtime));
	case EditorRuntimeCommandId::cancel_mirror:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, cancel_mirror(session, runtime));
	case EditorRuntimeCommandId::update_mirror_hover: {
		const auto *settings = std::get_if<MirrorRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_mirror_hover(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::click_mirror: {
		const auto *settings = std::get_if<MirrorRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, click_mirror(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::begin_mirror_point_drag: {
		const auto *settings = std::get_if<MirrorPointDragRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, begin_mirror_point_drag(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::begin_mirror_line_drag: {
		const auto *settings = std::get_if<MirrorRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, begin_mirror_line_drag(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::end_mirror_drag:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, end_mirror_drag(session, runtime));
	case EditorRuntimeCommandId::finalize_mirror:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, finalize_mirror(session, runtime));
	case EditorRuntimeCommandId::mirror_axis: {
		const auto *settings = std::get_if<MirrorAxisRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, mirror_axis(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::flip_view: {
		const auto *settings = std::get_if<ViewFlipRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, flip_view(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::create_box_from_bounds: {
		const auto *settings = std::get_if<CreateBoxFromBoundsRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, create_box_from_bounds(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::update_box_hover: {
		const auto *settings = std::get_if<BoxRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_box_hover(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::begin_box_footprint: {
		const auto *settings = std::get_if<BoxRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, begin_box_footprint(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::update_box_footprint: {
		const auto *settings = std::get_if<BoxRayRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_box_footprint(session, runtime, *settings));
	}
	case EditorRuntimeCommandId::begin_box_height: {
		const auto *settings = std::get_if<BoxHeightRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, begin_box_height(runtime, *settings));
	}
	case EditorRuntimeCommandId::update_box_height: {
		const auto *settings = std::get_if<BoxHeightRuntimeCommandSettings>(&command.settings);
		if (settings == nullptr) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, update_box_height(runtime, *settings));
	}
	case EditorRuntimeCommandId::commit_box:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, commit_box(session, runtime));
	case EditorRuntimeCommandId::cancel_box:
		if (!std::holds_alternative<std::monostate>(command.settings)) {
			return runtime_command_result(runtime, settings_mismatch_result());
		}
		return runtime_command_result(runtime, cancel_box(runtime));
	}

	return runtime_command_result(runtime, { false, false, "Unknown runtime command." });
}

} // namespace quader_editor_core

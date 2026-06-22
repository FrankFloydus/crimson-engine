////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <modeling/runtime/editor_runtime.h>
#include <modeling/runtime/session.h>
#include <modeling/runtime/tool_runtime_policy.h>
#include <modeling/interactions/interaction_api.h>
#include <modeling/polygon/document.h>

#include <string>

TEST_CASE("viewport tool ids are stable host contracts", "[editor-core][tool][activation]")
{
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::select)) == "select");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::move)) == "move");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::extend)) == "extend");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::extrude)) == "extrude");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::rotate)) == "rotate");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::scale)) == "scale");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::box)) == "box");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::insert_edge_loop)) == "insert_edge_loop");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::knife)) == "knife");
	CHECK(std::string(quader_editor_core::viewport_interaction_tool_id(quader_editor_core::ViewportInteractionTool::cut)) == "cut");

	CHECK(quader_editor_core::viewport_interaction_tool_from_id("select") == quader_editor_core::ViewportInteractionTool::select);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("move") == quader_editor_core::ViewportInteractionTool::move);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("extend") == quader_editor_core::ViewportInteractionTool::extend);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("extrude") == quader_editor_core::ViewportInteractionTool::extrude);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("rotate") == quader_editor_core::ViewportInteractionTool::rotate);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("scale") == quader_editor_core::ViewportInteractionTool::scale);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("box") == quader_editor_core::ViewportInteractionTool::box);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("insert_edge_loop") == quader_editor_core::ViewportInteractionTool::insert_edge_loop);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("knife") == quader_editor_core::ViewportInteractionTool::knife);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("cut") == quader_editor_core::ViewportInteractionTool::cut);
	CHECK(quader_editor_core::viewport_interaction_tool_from_id("missing") == quader_editor_core::ViewportInteractionTool::unknown);
}

TEST_CASE("tool activation policy resets conflicting runtime owners", "[editor-core][tool][activation]")
{
	SECTION("same tool activation preserves owned tool previews and clears stale transform drag")
	{
		quader_editor_core::ViewportToolActivationRequest same_tool;
		same_tool.current_tool = quader_editor_core::ViewportInteractionTool::move;
		same_tool.requested_tool = quader_editor_core::ViewportInteractionTool::move;
		const quader_editor_core::ViewportToolActivationPlan same_tool_plan =
			quader_editor_core::plan_viewport_tool_activation(same_tool);
		CHECK_FALSE(same_tool_plan.active_tool_changed);
		CHECK_FALSE(same_tool_plan.cancel_transform_preview);
		CHECK(same_tool_plan.reset_transform_drag);

		quader_editor_core::ViewportToolActivationRequest same_insert;
		same_insert.current_tool = quader_editor_core::ViewportInteractionTool::insert_edge_loop;
		same_insert.requested_tool = quader_editor_core::ViewportInteractionTool::insert_edge_loop;
		same_insert.selection_mode = quader_editor_core::SelectionMode::edge;
		const quader_editor_core::ViewportToolActivationPlan same_insert_plan =
			quader_editor_core::plan_viewport_tool_activation(same_insert);
		CHECK(same_insert_plan.insert_edge_loop_tool_active);
		CHECK_FALSE(same_insert_plan.reset_insert_edge_loop_tool);

		quader_editor_core::ViewportToolActivationRequest same_knife;
		same_knife.current_tool = quader_editor_core::ViewportInteractionTool::knife;
		same_knife.requested_tool = quader_editor_core::ViewportInteractionTool::knife;
		const quader_editor_core::ViewportToolActivationPlan same_knife_plan =
			quader_editor_core::plan_viewport_tool_activation(same_knife);
		CHECK(same_knife_plan.knife_tool_active);
		CHECK_FALSE(same_knife_plan.reset_knife_tool);

		quader_editor_core::ViewportToolActivationRequest same_cut;
		same_cut.current_tool = quader_editor_core::ViewportInteractionTool::cut;
		same_cut.requested_tool = quader_editor_core::ViewportInteractionTool::cut;
		same_cut.selected_cut_target_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan same_cut_plan =
			quader_editor_core::plan_viewport_tool_activation(same_cut);
		CHECK(same_cut_plan.cut_tool_active);
		CHECK_FALSE(same_cut_plan.reset_cut_tool);

		quader_editor_core::ViewportToolActivationRequest same_mirror;
		same_mirror.current_tool = quader_editor_core::ViewportInteractionTool::mirror;
		same_mirror.requested_tool = quader_editor_core::ViewportInteractionTool::mirror;
		same_mirror.selected_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan same_mirror_plan =
			quader_editor_core::plan_viewport_tool_activation(same_mirror);
		CHECK(same_mirror_plan.mirror_tool_active);
		CHECK_FALSE(same_mirror_plan.reset_mirror_tool);
	}

	SECTION("conflicting activation plans reset transient owners")
	{
		quader_editor_core::ViewportToolActivationRequest request;
		request.current_tool = quader_editor_core::ViewportInteractionTool::move;
		request.requested_tool = quader_editor_core::ViewportInteractionTool::cut;
		request.selection_mode = quader_editor_core::SelectionMode::face;
		request.transform_drag_active = true;
		request.selected_cut_target_object_count = 1;

		const quader_editor_core::ViewportToolActivationPlan plan = quader_editor_core::plan_viewport_tool_activation(request);
		REQUIRE(plan.active_tool == quader_editor_core::ViewportInteractionTool::cut);
		CHECK(plan.active_tool_changed);
		CHECK(plan.cancel_transform_preview);
		CHECK_FALSE(plan.reset_transform_drag);
		CHECK(plan.reset_insert_edge_loop_tool);
		CHECK(plan.reset_knife_tool);
		CHECK_FALSE(plan.knife_tool_active);
		CHECK(plan.cut_tool_active);
		CHECK(plan.reset_cut_tool);
		CHECK_FALSE(plan.insert_edge_loop_tool_active);
		CHECK(plan.reset_box_tool);
		CHECK(plan.reset_box_grid_size);
		CHECK(plan.clear_gizmo_hover);
		CHECK_FALSE(plan.selection_mode_changed);
	}

	SECTION("drawn plane object tools reject activation without required selection")
	{
		quader_editor_core::ViewportToolActivationRequest cut_request;
		cut_request.current_tool = quader_editor_core::ViewportInteractionTool::knife;
		cut_request.requested_tool = quader_editor_core::ViewportInteractionTool::cut;
		cut_request.transform_drag_active = true;

		const quader_editor_core::ViewportToolActivationPlan cut_plan =
			quader_editor_core::plan_viewport_tool_activation(cut_request);
		CHECK(cut_plan.activation_blocked);
		CHECK(cut_plan.active_tool == quader_editor_core::ViewportInteractionTool::knife);
		CHECK_FALSE(cut_plan.cancel_transform_preview);
		CHECK_FALSE(cut_plan.reset_transform_drag);
		CHECK_FALSE(cut_plan.reset_cut_tool);
		CHECK_FALSE(cut_plan.clear_gizmo_hover);
		CHECK(std::string(cut_plan.blocked_message) == "Select one or more mesh objects to cut.");

		cut_request.selected_cut_target_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan cut_allowed =
			quader_editor_core::plan_viewport_tool_activation(cut_request);
		CHECK_FALSE(cut_allowed.activation_blocked);
		CHECK(cut_allowed.active_tool == quader_editor_core::ViewportInteractionTool::cut);

		quader_editor_core::ViewportToolActivationRequest mirror_request;
		mirror_request.current_tool = quader_editor_core::ViewportInteractionTool::select;
		mirror_request.requested_tool = quader_editor_core::ViewportInteractionTool::mirror;
		mirror_request.selection_mode = quader_editor_core::SelectionMode::face;
		mirror_request.selected_object_count = 1;
		const quader_editor_core::ViewportToolActivationPlan mirror_component_mode =
			quader_editor_core::plan_viewport_tool_activation(mirror_request);
		CHECK(mirror_component_mode.activation_blocked);
		CHECK(std::string(mirror_component_mode.blocked_message) == "Select one or more mesh objects to mirror.");

		mirror_request.selection_mode = quader_editor_core::SelectionMode::object;
		const quader_editor_core::ViewportToolActivationPlan mirror_allowed =
			quader_editor_core::plan_viewport_tool_activation(mirror_request);
		CHECK_FALSE(mirror_allowed.activation_blocked);
		CHECK(mirror_allowed.active_tool == quader_editor_core::ViewportInteractionTool::mirror);
		CHECK(mirror_allowed.mirror_tool_active);
	}

	SECTION("insert edge loop activation owns edge selection without overwriting candidates")
	{
		quader_editor_core::ViewportToolActivationRequest request;
		request.current_tool = quader_editor_core::ViewportInteractionTool::select;
		request.requested_tool = quader_editor_core::ViewportInteractionTool::insert_edge_loop;
		request.selection_mode = quader_editor_core::SelectionMode::object;
		request.hover_has_object = true;
		request.hover_has_candidate_object = true;

		const quader_editor_core::ViewportToolActivationPlan plan = quader_editor_core::plan_viewport_tool_activation(request);
		CHECK(plan.selection_mode == quader_editor_core::SelectionMode::edge);
		CHECK(plan.insert_edge_loop_tool_active);
		CHECK_FALSE(plan.promote_hover_object_to_candidate);
	}

	SECTION("box activation preserves box state while clearing cut state")
	{
		quader_editor_core::ViewportToolActivationRequest request;
		request.current_tool = quader_editor_core::ViewportInteractionTool::cut;
		request.requested_tool = quader_editor_core::ViewportInteractionTool::box;

		const quader_editor_core::ViewportToolActivationPlan plan = quader_editor_core::plan_viewport_tool_activation(request);
		CHECK_FALSE(plan.reset_box_tool);
		CHECK_FALSE(plan.reset_box_grid_size);
		CHECK(plan.reset_knife_tool);
		CHECK(plan.reset_cut_tool);
	}

	CHECK(quader_editor_core::sanitize_viewport_interaction_tool(quader_editor_core::ViewportInteractionTool::unknown) ==
		quader_editor_core::ViewportInteractionTool::select);
	CHECK(quader_editor_core::viewport_tool_uses_transform_drag(quader_editor_core::ViewportInteractionTool::extrude));
	CHECK_FALSE(quader_editor_core::viewport_tool_uses_transform_gizmo(quader_editor_core::ViewportInteractionTool::extrude));
}

TEST_CASE("editor runtime activation waits for transform preview cancellation", "[editor-core][tool][activation]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::scale;
	runtime.transform_drag.active = true;
	runtime.transform_drag.axis = quader_editor_core::TransformAxis::x;
	runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::x;

	quader_editor_core::EditorRuntimeToolActivationRequest request;
	request.requested_tool = quader_editor_core::ViewportInteractionTool::insert_edge_loop;
	request.selection_mode = quader_editor_core::SelectionMode::object;
	request.transform_preview_active = true;

	const quader_editor_core::EditorRuntimeToolActivationResult blocked =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);
	REQUIRE_FALSE(blocked.applied);
	REQUIRE(blocked.requires_transform_preview_cancel);
	CHECK(runtime.active_tool == quader_editor_core::ViewportInteractionTool::scale);
	CHECK(runtime.transform_drag.active);
	CHECK(runtime.gizmo_hover_axis == quader_editor_core::TransformAxis::x);

	request.transform_preview_cleared = true;
	const quader_editor_core::EditorRuntimeToolActivationResult applied =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);
	REQUIRE(applied.applied);
	CHECK_FALSE(applied.requires_transform_preview_cancel);
	CHECK(runtime.active_tool == quader_editor_core::ViewportInteractionTool::insert_edge_loop);
	CHECK(runtime.insert_edge_loop.active);
	CHECK(applied.plan.selection_mode == quader_editor_core::SelectionMode::edge);
	CHECK_FALSE(runtime.transform_drag.active);
	CHECK(runtime.gizmo_hover_axis == quader_editor_core::TransformAxis::none);
}

TEST_CASE("editor runtime activation blocks selected-object tools before mutating state", "[editor-core][tool][activation]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::scale;
	runtime.transform_drag.active = true;
	runtime.transform_drag.axis = quader_editor_core::TransformAxis::x;
	runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::x;
	runtime.cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };

	quader_editor_core::EditorRuntimeToolActivationRequest request;
	request.requested_tool = quader_editor_core::ViewportInteractionTool::cut;
	request.selection_mode = quader_editor_core::SelectionMode::object;
	request.transform_preview_active = true;

	const quader_editor_core::EditorRuntimeToolActivationResult blocked =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);

	REQUIRE_FALSE(blocked.applied);
	CHECK_FALSE(blocked.requires_transform_preview_cancel);
	CHECK(blocked.plan.activation_blocked);
	CHECK(std::string(blocked.plan.blocked_message) == "Select one or more mesh objects to cut.");
	CHECK(runtime.active_tool == quader_editor_core::ViewportInteractionTool::scale);
	CHECK(runtime.transform_drag.active);
	CHECK(runtime.gizmo_hover_axis == quader_editor_core::TransformAxis::x);
	REQUIRE(runtime.cut.points.size() == 2U);

	request.selected_cut_target_object_count = 1;
	const quader_editor_core::EditorRuntimeToolActivationResult pending_cancel =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);

	REQUIRE_FALSE(pending_cancel.applied);
	CHECK(pending_cancel.requires_transform_preview_cancel);
	CHECK_FALSE(pending_cancel.plan.activation_blocked);
	CHECK(runtime.active_tool == quader_editor_core::ViewportInteractionTool::scale);
}

TEST_CASE("editor runtime status and reset policies report transient owners", "[editor-core][tool][activation]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::move;
	runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::x;
	runtime.hover.object_id = 7;
	runtime.hover.candidate_object_id = 7;
	runtime.box.preview_valid = true;
	runtime.box_grid_size = 2.0;
	runtime.insert_edge_loop.active = true;
	runtime.insert_edge_loop.dragging = true;
	runtime.insert_edge_loop.preview_lines.push_back({});
	runtime.knife.active = true;
	runtime.knife.has_active_stroke = true;
	runtime.knife.has_current = true;
	runtime.transform_drag.active = true;
	runtime.clipboard_objects.push_back({ 99, 0, true, quader_poly::make_cube({}), {} });

	const quader_editor_core::EditorRuntimeStatus status = quader_editor_core::editor_runtime_status(runtime);
	CHECK(status.active_tool == quader_editor_core::ViewportInteractionTool::move);
	CHECK(status.gizmo_hover_axis == quader_editor_core::TransformAxis::x);
	CHECK(status.hover_active);
	CHECK(status.hover_has_candidate_object);
	CHECK(status.box_active);
	CHECK(status.box_grid_size == 2.0);
	CHECK_FALSE(status.box_grid_at_default);
	CHECK(status.insert_edge_loop_active);
	CHECK(status.insert_edge_loop_dragging);
	CHECK(status.insert_edge_loop_has_preview);
	CHECK(status.knife_active);
	CHECK(status.knife_has_active_stroke);
	CHECK(status.knife_has_current);
	CHECK(status.transform_drag_active);
	CHECK(status.clipboard_available);
	CHECK(status.clipboard_object_count == 1);

	quader_editor_core::clear_editor_runtime_preview_presentation_state(runtime);
	const quader_editor_core::EditorRuntimeStatus cleared = quader_editor_core::editor_runtime_status(runtime);
	CHECK(runtime.active_tool == quader_editor_core::ViewportInteractionTool::move);
	CHECK(runtime.box_grid_size == 2.0);
	CHECK_FALSE(cleared.hover_active);
	CHECK_FALSE(cleared.box_active);
	CHECK_FALSE(cleared.insert_edge_loop_active);
	CHECK_FALSE(cleared.knife_active);
	CHECK_FALSE(cleared.transform_drag_active);
	CHECK(cleared.gizmo_hover_axis == quader_editor_core::TransformAxis::none);
	CHECK(cleared.clipboard_available);
}

TEST_CASE("editor runtime selection presentation clear owns hover gizmo and drag ordering", "[editor-core][tool][activation]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::move;
	runtime.hover.object_id = 7;
	runtime.hover.candidate_object_id = 7;
	runtime.gizmo_hover_axis = quader_editor_core::TransformAxis::x;
	runtime.transform_drag.active = true;
	runtime.transform_drag.axis = quader_editor_core::TransformAxis::x;
	runtime.box.preview_valid = true;
	runtime.box_grid_size = 4.0;
	runtime.insert_edge_loop.active = true;
	runtime.insert_edge_loop.preview_lines.push_back({});
	runtime.clipboard_objects.push_back({ 99, 0, true, quader_poly::make_cube({}), {} });

	quader_editor_core::clear_editor_runtime_selection_presentation_state(runtime);

	const quader_editor_core::EditorRuntimeStatus status = quader_editor_core::editor_runtime_status(runtime);
	CHECK(status.active_tool == quader_editor_core::ViewportInteractionTool::move);
	CHECK_FALSE(status.hover_active);
	CHECK(status.gizmo_hover_axis == quader_editor_core::TransformAxis::none);
	CHECK_FALSE(status.transform_drag_active);
	CHECK(status.box_active);
	CHECK(status.box_grid_size == 4.0);
	CHECK(status.insert_edge_loop_active);
	CHECK(status.insert_edge_loop_has_preview);
	CHECK(status.clipboard_available);

	quader_editor_core::EditorRuntime wrapper;
	quader_editor_core::SelectionHoverState hover;
	hover.object_id = 11;
	wrapper.set_hover(hover);
	wrapper.set_gizmo_hover_axis(quader_editor_core::TransformAxis::z);
	wrapper.begin_transform_drag_state(
		quader_editor_core::ViewportInteractionTool::rotate,
		quader_editor_core::TransformDragOperation::transform,
		quader_editor_core::TransformAxis::z,
		{});

	wrapper.clear_selection_presentation_state();

	CHECK_FALSE(wrapper.status().hover_active);
	CHECK(wrapper.status().gizmo_hover_axis == quader_editor_core::TransformAxis::none);
	CHECK_FALSE(wrapper.status().transform_drag_active);
}

TEST_CASE("editor runtime document reset clears session preview and runtime tool state", "[editor-core][tool][activation]")
{
	quader_editor_core::EditorSession session;
	const quader_editor_core::CommandResult preview_result = session.begin_transform_preview(false);
	REQUIRE(preview_result.ok);
	REQUIRE(session.preview_transaction_status().active);

	quader_editor_core::EditorRuntime runtime;
	runtime.state().active_tool = quader_editor_core::ViewportInteractionTool::scale;
	runtime.state().gizmo_hover_axis = quader_editor_core::TransformAxis::y;
	runtime.state().box_grid_size = 16.0;
	runtime.state().transform_drag.active = true;
	runtime.state().clipboard_objects.push_back({ 4, 0, false, quader_poly::make_cube({}), {} });

	runtime.reset_document_state(session);
	const quader_editor_core::EditorRuntimeStatus status = runtime.status();
	CHECK_FALSE(session.preview_transaction_status().active);
	CHECK(status.active_tool == quader_editor_core::ViewportInteractionTool::select);
	CHECK(status.gizmo_hover_axis == quader_editor_core::TransformAxis::none);
	CHECK_FALSE(status.transform_drag_active);
	CHECK(runtime.state().box_grid_size == quader_editor_core::default_editor_runtime_box_grid_size);
	CHECK(status.clipboard_available);
}

TEST_CASE("editor runtime set active tool owns preview cancellation and selection mode", "[editor-core][tool][activation]")
{
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	session.set_selected_object_ids({ 1 }, 1);
	REQUIRE(session.begin_transform_preview(false).ok);

	quader_editor_core::EditorRuntime runtime;
	runtime.state().active_tool = quader_editor_core::ViewportInteractionTool::move;
	runtime.state().transform_drag.active = true;
	runtime.state().transform_drag.axis = quader_editor_core::TransformAxis::y;
	runtime.state().gizmo_hover_axis = quader_editor_core::TransformAxis::y;
	runtime.state().hover.object_id = 1;

	const quader_editor_core::EditorRuntimeToolActivationResult result =
		runtime.set_active_tool(session, quader_editor_core::ViewportInteractionTool::insert_edge_loop);

	REQUIRE(result.applied);
	CHECK(result.transform_preview_cancelled);
	CHECK(result.selection_mode_applied);
	CHECK_FALSE(session.preview_transaction_status().active);
	CHECK(session.selection_mode() == quader_editor_core::SelectionMode::edge);
	CHECK(runtime.active_tool() == quader_editor_core::ViewportInteractionTool::insert_edge_loop);
	CHECK(runtime.state().insert_edge_loop.active);
	CHECK_FALSE(runtime.state().transform_drag.active);
	CHECK(runtime.state().gizmo_hover_axis == quader_editor_core::TransformAxis::none);
	CHECK(runtime.state().hover.candidate_object_id == 1);
}

TEST_CASE("editor runtime wrapper methods keep state ownership in compiled source", "[editor-core][tool][activation]")
{
	quader_editor_core::EditorRuntimeState initial;
	initial.active_tool = quader_editor_core::ViewportInteractionTool::box;
	initial.box_grid_size = 4.0;

	quader_editor_core::EditorRuntime runtime(initial);
	CHECK(runtime.active_tool() == quader_editor_core::ViewportInteractionTool::box);
	CHECK(runtime.box_grid_size() == 4.0);

	quader_editor_core::SelectionHoverState hover;
	hover.object_id = 7;
	runtime.set_hover(hover);
	CHECK(runtime.status().hover_active);

	quader_editor_core::SelectionHoverUpdateResult hover_update;
	hover_update.hover.object_id = 8;
	hover_update.hover.candidate_object_id = 8;
	runtime.apply_hover_update(hover_update);
	CHECK(runtime.status().hover_has_candidate_object);
	CHECK(runtime.hover().object_id == 8);
	CHECK(runtime.hover().candidate_object_id == 8);

	runtime.clear_hover();
	CHECK_FALSE(runtime.status().hover_active);

	quader_editor_core::BoxToolState box;
	box.preview_valid = true;
	runtime.set_box_state(box);
	CHECK(runtime.status().box_active);
	CHECK(runtime.box_active());

	runtime.reset_box_state();
	CHECK_FALSE(runtime.status().box_active);
	CHECK(runtime.box_grid_size() == quader_editor_core::default_editor_runtime_box_grid_size);
	runtime.set_box_grid_size(0.5);
	REQUIRE(runtime.set_box_construction_plane({ 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }).ok);
	const quader_editor_core::BoxConstructionPointResult box_point =
		runtime.box_construction_point_from_ray({ { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } });
	REQUIRE(box_point.ok);
	REQUIRE(runtime.begin_box_footprint(box_point.position, box_point.position, true).ok);
	REQUIRE(runtime.update_box_footprint({ 1.0F, 0.0F, 1.0F }, true).ok);
	CHECK(runtime.box_preview_descriptor(true).ok);
	CHECK(runtime.box_state().drag_state == quader_editor_core::BoxDragState::footprint);

	runtime.begin_transform_drag_state(
		quader_editor_core::ViewportInteractionTool::scale,
		quader_editor_core::TransformDragOperation::transform,
		quader_editor_core::TransformAxis::y,
		{ 1.0F, 2.0F, 3.0F });
	CHECK(runtime.status().transform_drag_active);
	CHECK(runtime.transform_drag_active());
	CHECK(runtime.transform_drag_state().tool == quader_editor_core::ViewportInteractionTool::scale);
	CHECK(runtime.transform_drag_state().axis == quader_editor_core::TransformAxis::y);

	quader_editor_core::ScaleToolState scale_state;
	scale_state.preview_scale = { 2.0F, 2.0F, 2.0F };
	runtime.set_transform_drag_scale_state(scale_state);
	CHECK(runtime.transform_drag_state().scale.preview_scale.x == 2.0F);

	runtime.clear_transform_drag_state();
	CHECK_FALSE(runtime.status().transform_drag_active);

	quader_editor_core::InsertEdgeLoopToolState insert_edge_loop;
	insert_edge_loop.active = true;
	runtime.set_insert_edge_loop_state(insert_edge_loop);
	runtime.set_insert_edge_loop_dragging(true);
	CHECK(runtime.status().insert_edge_loop_active);
	CHECK(runtime.status().insert_edge_loop_dragging);
	runtime.set_insert_edge_loop_active(false);
	CHECK_FALSE(runtime.insert_edge_loop_state().active);

	quader_editor_core::KnifeToolState knife;
	knife.active = true;
	knife.has_current = true;
	runtime.set_knife_state(knife);
	CHECK(runtime.status().knife_active);
	CHECK(runtime.status().knife_has_current);

	runtime.set_knife_active(false);
	CHECK_FALSE(runtime.status().knife_active);

	quader_editor_core::CutHoverState cut_hover;
	cut_hover.valid = true;
	cut_hover.object_id = 3;
	runtime.set_cut_hover(cut_hover);
	CHECK(runtime.cut_hover().valid);
	CHECK(runtime.cut_hover().object_id == 3);
	runtime.clear_cut_hover();
	CHECK_FALSE(runtime.cut_hover().valid);

	quader_editor_core::CutToolState cut;
	cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	cut.target_object_ids = { 1 };
	runtime.set_cut_state(cut);
	CHECK(runtime.cut_state().points.size() == 2U);
	REQUIRE(runtime.set_cut_mode(quader_editor_core::CutMode::keep_front).ok);
	CHECK(runtime.cut_state().mode == quader_editor_core::CutMode::keep_front);

	quader_editor_core::PolyToolState poly;
	poly.active = true;
	poly.closed = true;
	poly.has_current = true;
	poly.points.push_back({ 0.0F, 0.0F, 0.0F });
	poly.points.push_back({ 1.0F, 0.0F, 0.0F });
	poly.points.push_back({ 1.0F, 0.0F, 1.0F });
	runtime.set_poly_state(poly);
	CHECK(runtime.status().poly_active);
	CHECK(runtime.status().poly_closed);
	CHECK(runtime.status().poly_can_commit);
	CHECK(runtime.poly_state().points.size() == 3U);
	runtime.set_poly_active(false);
	CHECK_FALSE(runtime.poly_state().active);

	runtime.set_gizmo_hover_axis(quader_editor_core::TransformAxis::z);
	CHECK(runtime.status().gizmo_hover_axis == quader_editor_core::TransformAxis::z);
	CHECK(runtime.gizmo_hover_axis() == quader_editor_core::TransformAxis::z);

	quader_editor_core::TransformGizmoPickHit miss;
	runtime.set_gizmo_hover_from_pick(miss);
	CHECK(runtime.status().gizmo_hover_axis == quader_editor_core::TransformAxis::none);

	quader_editor_core::TransformGizmoPickHit hit;
	hit.hit = true;
	hit.axis = quader_editor_core::TransformAxis::x;
	runtime.set_gizmo_hover_from_pick(hit);
	CHECK(runtime.status().gizmo_hover_axis == quader_editor_core::TransformAxis::x);

	runtime.set_clipboard({ { 42, 0, true, quader_poly::make_cube({}), {} } });
	CHECK(runtime.clipboard_available());
	REQUIRE(runtime.clipboard_objects().size() == 1U);
	CHECK(runtime.clipboard_object_count() == 1U);
	CHECK(runtime.status().clipboard_object_count == 1U);

	runtime.clear_clipboard();
	CHECK_FALSE(runtime.clipboard_available());

	quader_editor_core::EditorRuntimeState replacement;
	replacement.active_tool = quader_editor_core::ViewportInteractionTool::select;
	replacement.box_grid_size = 2.0;
	runtime.set_state(replacement);
	CHECK(runtime.active_tool() == quader_editor_core::ViewportInteractionTool::select);
	CHECK(runtime.box_grid_size() == 2.0);
}

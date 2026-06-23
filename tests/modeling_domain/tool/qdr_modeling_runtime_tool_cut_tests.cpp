////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <editor/interactions/cut_tool.h>
#include <editor/modeling/runtime/editor_runtime.h>
#include <editor/interactions/interaction_api.h>

#include <vector>

TEST_CASE("cut tool captures target objects from current selection", "[editor-core][tool][cut]")
{
	quader_editor_core::RectangleSelectionState selection;
	selection.selected_object_ids = { 4, 8 };
	selection.active_object_id = 4;

	quader_editor_core::CutToolState state;
	const quader_editor_core::CutToolResult first =
		quader_editor_core::capture_first_cut_point(state, selection, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
	REQUIRE(first.ok);
	CHECK(first.changed);
	CHECK(first.target_count == 2);
	REQUIRE(state.target_object_ids.size() == 2);
	CHECK(state.target_object_ids[0] == 4);
	CHECK(state.target_object_ids[1] == 8);
	CHECK(state.source_object_id == -1);
	CHECK(state.dragging_new_point);
	CHECK(quader_editor_core::cut_is_active(state));
	CHECK(quader_editor_core::cut_is_dragging(state));
}

TEST_CASE("cut and knife runtime helpers keep separate ownership", "[editor-core][tool][cut]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::knife;
	runtime.knife.active = true;
	runtime.knife.has_current = true;

	quader_editor_core::reset_editor_runtime_knife_state(runtime, true);
	CHECK(runtime.knife.active);
	CHECK_FALSE(runtime.knife.has_current);

	quader_editor_core::set_editor_runtime_knife_active(runtime, false);
	CHECK_FALSE(runtime.knife.active);
	quader_editor_core::set_editor_runtime_knife_active(runtime, true);
	CHECK(runtime.knife.active);

	quader_editor_core::EditorRuntime editor_runtime;
	quader_editor_core::KnifeToolState knife;
	knife.active = true;
	knife.has_current = true;
	knife.status = quader_editor_core::KnifeToolStatus::preview;
	editor_runtime.set_knife_state(knife);
	CHECK(editor_runtime.status().knife_active);
	CHECK(editor_runtime.status().knife_has_current);

	editor_runtime.set_knife_active(false);
	CHECK_FALSE(editor_runtime.status().knife_active);

	runtime.active_tool = quader_editor_core::ViewportInteractionTool::cut;
	runtime.cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	runtime.cut.target_object_ids = { 1 };
	runtime.cut_hover.valid = true;
	runtime.cut_hover.object_id = 1;
	runtime.cut_hover.position = { 0.5F, 0.0F, 0.0F };
	CHECK(quader_editor_core::editor_runtime_status(runtime).cut_active);
	CHECK(quader_editor_core::editor_runtime_status(runtime).cut_can_preview);

	runtime.active_tool = quader_editor_core::ViewportInteractionTool::select;
	quader_editor_core::reset_editor_runtime_cut_state(runtime, false);
	CHECK_FALSE(quader_editor_core::editor_runtime_status(runtime).cut_active);
	CHECK_FALSE(runtime.cut_hover.valid);
	CHECK(runtime.cut_hover.object_id == -1);
}

TEST_CASE("cut hover is durable editor runtime state cleared by activation resets", "[editor-core][tool][cut]")
{
	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::cut;
	runtime.cut.points = { { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F } };
	runtime.cut.target_object_ids = { 1 };
	runtime.cut_hover.valid = true;
	runtime.cut_hover.object_id = 1;
	runtime.cut_hover.position = { 0.5F, 0.0F, 0.0F };

	quader_editor_core::EditorRuntimeToolActivationRequest request;
	request.requested_tool = quader_editor_core::ViewportInteractionTool::knife;
	const quader_editor_core::EditorRuntimeToolActivationResult result =
		quader_editor_core::activate_editor_runtime_tool(runtime, request);

	REQUIRE(result.applied);
	CHECK(result.plan.reset_cut_tool);
	CHECK(runtime.active_tool == quader_editor_core::ViewportInteractionTool::knife);
	CHECK_FALSE(quader_editor_core::cut_is_active(runtime.cut));
	CHECK_FALSE(runtime.cut_hover.valid);
	CHECK(runtime.cut_hover.object_id == -1);
}

TEST_CASE("cut tool uses two snapped world-grid points", "[editor-core][tool][cut]")
{
	quader_editor_core::RectangleSelectionState selection;
	selection.mode = quader_editor_core::SelectionMode::face;
	selection.active_object_id = 5;
	selection.selected_faces = { { 5, 2 } };

	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;
	const quader_editor_core::CutGridPointResult grid_point =
		quader_editor_core::cut_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5);
	REQUIRE(grid_point.ok);
	CHECK(grid_point.position.x == 0.5F);
	CHECK(grid_point.position.y == 0.0F);
	CHECK(grid_point.position.z == 1.5F);

	quader_editor_core::CutToolState state;
	REQUIRE(quader_editor_core::capture_first_cut_point(state, selection, grid_point.position, grid_point.plane_normal).ok);
	CHECK_FALSE(quader_editor_core::capture_second_cut_point(state, grid_point.position).ok);
	REQUIRE(quader_editor_core::capture_second_cut_point(state, { 1.5F, 0.0F, 1.5F }).ok);

	REQUIRE(state.points.size() == 2);
	REQUIRE(state.target_object_ids.size() == 1);
	CHECK(state.target_object_ids[0] == 5);
	CHECK(quader_editor_core::cut_can_preview(state));
	const std::vector<quader::QVec3> plane_points = quader_editor_core::cut_plane_points(state);
	REQUIRE(plane_points.size() == 3);
	CHECK(plane_points[2].y > plane_points[0].y);

	const std::vector<quader::QVec3> overlay_points =
		quader_editor_core::cut_overlay_points(state, { 2.0F, 0.0F, 2.0F }, true);
	REQUIRE(overlay_points.size() == 2);
	CHECK(overlay_points[0].x == state.points[0].x);
	CHECK(overlay_points[0].y == state.points[0].y);
	CHECK(overlay_points[0].z == state.points[0].z);
	CHECK(overlay_points[1].x == state.points[1].x);
	CHECK(overlay_points[1].y == state.points[1].y);
	CHECK(overlay_points[1].z == state.points[1].z);
}

TEST_CASE("cut ray policy updates runtime-owned cut state and hover together", "[editor-core][tool][cut]")
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::RectangleSelectionState selection;
	selection.mode = quader_editor_core::SelectionMode::object;
	selection.selected_object_ids = { 7 };
	selection.active_object_id = 7;

	quader_editor_core::EditorRuntimeState runtime;
	runtime.active_tool = quader_editor_core::ViewportInteractionTool::cut;

	quader_editor_core::CutRayUpdateInput first_input;
	first_input.camera.projection = quader_editor_core::CameraProjection::perspective;
	first_input.ray = { { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	first_input.grid_size = 0.5;

	const quader_editor_core::CutToolResult hover_result =
		quader_editor_core::update_cut_hover_from_ray(
			runtime,
			selection,
			objects,
			first_input);
	REQUIRE(hover_result.ok);
	CHECK(hover_result.changed);
	CHECK(runtime.cut_hover.valid);
	CHECK(runtime.cut_hover.position.x == 0.0F);
	CHECK(runtime.cut_hover.position.y == 0.0F);
	CHECK(runtime.cut_hover.position.z == 0.0F);
	REQUIRE(runtime.cut.target_object_ids.size() == 1U);
	CHECK(runtime.cut.target_object_ids[0] == 7);

	const quader_editor_core::CutToolResult first_click =
		quader_editor_core::click_cut_from_ray(
			runtime,
			selection,
			objects,
			first_input);
	REQUIRE(first_click.ok);
	REQUIRE(runtime.cut.points.size() == 1U);
	CHECK(runtime.cut.dragging_new_point);
	CHECK(runtime.cut_hover.valid);

	quader_editor_core::CutRayUpdateInput second_input = first_input;
	second_input.ray = { { 1.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	const quader_editor_core::CutToolResult second_click =
		quader_editor_core::click_cut_from_ray(
			runtime,
			selection,
			objects,
			second_input);
	REQUIRE(second_click.ok);
	REQUIRE(runtime.cut.points.size() == 2U);
	CHECK_FALSE(runtime.cut.dragging_new_point);
	CHECK(runtime.cut.dragging_point_index == 1);
	CHECK(quader_editor_core::cut_preview_metadata(runtime.cut).preview_valid);
}

TEST_CASE("cut preview metadata invalidates on mode and point changes", "[editor-core][tool][cut]")
{
	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	state.target_object_ids = { 1 };

	const quader_editor_core::CutPreviewRequest request =
		quader_editor_core::cut_preview_request(state, quader_editor_core::CutMode::keep_both);
	REQUIRE(request.ok);
	CHECK(request.can_preview);
	CHECK(request.point_count == 2);
	CHECK(request.target_count == 1);
	CHECK(request.object_ids[0] == 1);

	quader_editor_core::CutPreviewCounts counts;
	counts.target_count = 1;
	counts.split_count = 1;
	counts.removed_count = 0;
	REQUIRE(quader_editor_core::set_cut_preview_result(state, quader_editor_core::CutMode::keep_both, true, counts).ok);
	CHECK(quader_editor_core::cut_preview_metadata(state).preview_valid);

	const quader_editor_core::CutToolResult mode_change =
		quader_editor_core::set_cut_mode(state, quader_editor_core::CutMode::keep_front);
	CHECK(mode_change.changed);
	CHECK_FALSE(quader_editor_core::cut_preview_metadata(state).preview_valid);

	const quader_editor_core::CutToolResult committed =
		quader_editor_core::commit_reset_cut(state, { 2, 1 });
	REQUIRE(committed.ok);
	CHECK(committed.target_count == 1);
	CHECK(committed.dirty_object_ids.size() == 2);
	CHECK_FALSE(quader_editor_core::cut_is_active(state));
}

TEST_CASE("cut preview policy evaluates target meshes outside native transport", "[editor-core][tool][cut]")
{
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 0.0F, 1.0F };
	state.target_object_ids = { 7 };
	state.mode = quader_editor_core::CutMode::keep_both;

	const quader_editor_core::CutToolResult result =
		quader_editor_core::refresh_cut_preview(state, objects);
	REQUIRE(result.ok);

	const quader_editor_core::CutPreviewMetadata metadata =
		quader_editor_core::cut_preview_metadata(state);
	CHECK(metadata.preview_valid);
	CHECK(metadata.target_count == 1);
	CHECK(metadata.split_count == 1);
	CHECK(metadata.removed_count == 0);

	const std::vector<quader_poly::Document> keep_documents =
		quader_editor_core::cut_preview_documents(
			state,
			objects,
			quader_editor_core::CutPreviewMeshSide::keep);
	const std::vector<quader_poly::Document> discard_documents =
		quader_editor_core::cut_preview_documents(
			state,
			objects,
			quader_editor_core::CutPreviewMeshSide::discard);

	REQUIRE(keep_documents.size() == 1U);
	REQUIRE(discard_documents.size() == 1U);
	CHECK_FALSE(quader_poly::compile_document(keep_documents.front()).vertices.empty());
	CHECK_FALSE(quader_poly::compile_document(discard_documents.front()).vertices.empty());
}

TEST_CASE("cut line drag moves the whole placed plane", "[editor-core][tool][cut]")
{
	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 1.0F, 0.0F };
	state.target_object_ids = { 1 };
	state.preview_valid = true;

	REQUIRE(quader_editor_core::begin_cut_line_drag(state, { 0.5F, 0.0F, 0.0F }).ok);
	CHECK(quader_editor_core::cut_is_dragging(state));

	const quader_editor_core::CutToolResult moved =
		quader_editor_core::update_dragged_cut_line(state, { 1.5F, 0.0F, 0.0F });
	REQUIRE(moved.ok);
	CHECK(moved.changed);
	REQUIRE(state.points.size() == 2U);
	CHECK(state.points[0].x == 1.0F);
	CHECK(state.points[0].y == 0.0F);
	CHECK(state.points[0].z == 0.0F);
	CHECK(state.points[1].x == 2.0F);
	CHECK(state.points[1].y == 0.0F);
	CHECK(state.points[1].z == 0.0F);
	CHECK(state.face_normal.y == 1.0F);
	CHECK_FALSE(state.preview_valid);

	REQUIRE(quader_editor_core::end_cut_line_drag(state).ok);
	CHECK_FALSE(quader_editor_core::cut_is_dragging(state));
}

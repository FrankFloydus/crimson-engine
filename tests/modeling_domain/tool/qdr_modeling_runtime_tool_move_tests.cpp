////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <mesh/polygon/document.hpp>

#include <vector>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;
	camera.transform.origin = { 0.0F, 0.0F, 6.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

quader_editor_core::PolygonObject cube_object(
		int id,
		quader::QVec3 offset = {},
		bool selected = false)
{
	quader_editor_core::PolygonObject object;
	object.id = id;
	object.selected = selected;
	object.document = quader_poly::make_cube({});
	for (quader_poly::Vertex& vertex : object.document.vertices) {
		vertex.position.x += offset.x;
		vertex.position.y += offset.y;
		vertex.position.z += offset.z;
	}
	return object;
}

quader_editor_core::MoveToolState begin_axis_move(
		quader_editor_core::TransformAxis axis,
		quader::QVec3 center)
{
	quader_editor_core::MoveBeginInput input;
	input.axis = axis;
	input.center_local = center;
	input.has_world_center = true;
	input.world_center = center;
	input.has_screen_axis = true;
	input.screen_pivot = { 100.0F, 100.0F };
	input.screen_tip = axis == quader_editor_core::TransformAxis::y ?
		quader::QVec2 { 100.0F, 200.0F } :
		quader::QVec2 { 200.0F, 100.0F };
	input.screen_position = { 100.0F, 100.0F };
	const quader_editor_core::MoveBeginResult begin = quader_editor_core::begin_move_drag(input);
	EXPECT_TRUE(begin.ok);
	return begin.state;
}

} // namespace

TEST(QdrModelingRuntimeToolMoveTests, MoveDragBeginsWithLocalAndScreenAxisState) {
	quader_editor_core::MoveBeginInput input;
	input.axis = quader_editor_core::TransformAxis::x;
	input.center_local = { 2.0F, 0.0F, 0.0F };
	input.has_world_center = true;
	input.world_center = { 5.0F, 0.0F, 0.0F };
	input.has_screen_axis = true;
	input.screen_pivot = { 10.0F, 20.0F };
	input.screen_tip = { 110.0F, 20.0F };
	input.screen_position = { 12.0F, 22.0F };

	const quader_editor_core::MoveBeginResult begin = quader_editor_core::begin_move_drag(input);
	EXPECT_TRUE(begin.ok);
	EXPECT_TRUE(begin.state.active);
	EXPECT_TRUE(begin.state.axis == quader_editor_core::TransformAxis::x);
	EXPECT_NEAR(begin.state.center_world.x, 5.0F, 0.0001);
	EXPECT_NEAR(begin.state.screen_axis.x, 1.0F, 0.0001);
	EXPECT_NEAR(begin.state.screen_axis.y, 0.0F, 0.0001);

	quader_editor_core::MoveToolState state = begin.state;
	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_drag_pixels(state, 64.0, false, true, 0.5);
	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_distance, 0.5, 0.0001);
	EXPECT_NEAR(preview.preview_delta.x, 0.5F, 0.0001);
	EXPECT_NEAR(preview.preview_delta.y, 0.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolMoveTests, ObjectMoveDragSnapsSelectionCenterToGridIntersections) {
	quader_editor_core::MoveToolState state =
		begin_axis_move(quader_editor_core::TransformAxis::x, { 0.25F, 0.0F, 0.0F });

	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_viewport(
			state,
			{ 164.0F, 100.0F },
			{},
			front_camera(),
			false,
			true,
			1.0,
			true);

	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_delta.x, 0.75F, 0.0001);
	EXPECT_NEAR(state.center_world.x + preview.preview_delta.x, 1.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolMoveTests, MoveDragSnapsPreviewDeltaToHoveredVertexTarget) {
	quader_editor_core::MoveToolState state =
		begin_axis_move(quader_editor_core::TransformAxis::y, { 2.0F, 3.0F, 4.0F });

	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_to_axis_snap_target(state, { 99.0F, 7.0F, -30.0F });

	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_distance, 4.0, 0.0001);
	EXPECT_NEAR(preview.preview_delta.x, 0.0F, 0.0001);
	EXPECT_NEAR(preview.preview_delta.y, 4.0F, 0.0001);
	EXPECT_NEAR(preview.preview_delta.z, 0.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolMoveTests, MoveDragSnapsSelectedAnchorToVertexTargetOnAnotherMesh) {
	quader_editor_core::PolygonObject moving = cube_object(1, {}, true);
	quader_editor_core::PolygonObject target = cube_object(2, { 0.0F, 4.0F, 0.0F }, false);
	quader_editor_core::EditorSession session({ moving, target });
	session.set_selected_object_ids({ 1 }, 1);

	quader_editor_core::TransformDragState drag;
	drag.active = true;
	drag.tool = quader_editor_core::ViewportInteractionTool::move;
	drag.operation = quader_editor_core::TransformDragOperation::transform;
	drag.axis = quader_editor_core::TransformAxis::y;
	drag.scene_transform_preview_eligible = true;
	drag.move = begin_axis_move(quader_editor_core::TransformAxis::y, {});

	quader_editor_core::TransformDragPreviewInput input;
	input.camera = front_camera();
	input.screen_position = { 100.0F, 100.0F };
	input.ray = { { 1.0F, 5.0F, 6.0F }, { 0.0F, 0.0F, -1.0F } };
	input.vertex_snap_enabled = true;
	input.vertex_snap_radius = 0.2;

	const quader_editor_core::CommandResult result =
		quader_editor_core::apply_transform_drag_preview(session, drag, input);

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(drag.snap_target.kind == quader_editor_core::TransformSnapLandmarkKind::vertex);
	EXPECT_TRUE(drag.snap_target.object_id == 2);
	EXPECT_NEAR(drag.move.preview_delta.x, 0.0F, 0.0001);
	EXPECT_NEAR(drag.move.preview_delta.y, 5.0F, 0.0001);
	EXPECT_NEAR(drag.move.preview_delta.z, 0.0F, 0.0001);

	const std::vector<quader_editor_core::SelectionOverlayPoint> points =
		quader_editor_core::transform_drag_snap_overlay_points(drag);
	EXPECT_TRUE(points.size() == 2U);
	EXPECT_TRUE(points[0].layer == quader_editor_core::SelectionOverlayLayer::hover_vertex);
	EXPECT_TRUE(points[0].object_id == 2);
	EXPECT_TRUE(points[0].color_abgr == quader_editor_core::selection_overlay_style_token(
		quader_editor_core::SelectionOverlayStyleRole::hover_vertex));
	EXPECT_TRUE(points[1].layer == quader_editor_core::SelectionOverlayLayer::snap_target_vertex);
	EXPECT_TRUE(points[1].object_id == 2);
	EXPECT_TRUE(points[1].color_abgr == quader_editor_core::selection_overlay_style_token(
		quader_editor_core::SelectionOverlayStyleRole::snap_target_vertex));
}

TEST(QdrModelingRuntimeToolMoveTests, MoveDragIgnoresVertexSnapWhenNoTargetIsInsideSnapRadius) {
	quader_editor_core::EditorSession session({ cube_object(1, {}, true) });
	session.set_selected_object_ids({ 1 }, 1);

	quader_editor_core::TransformDragState drag;
	drag.active = true;
	drag.tool = quader_editor_core::ViewportInteractionTool::move;
	drag.operation = quader_editor_core::TransformDragOperation::transform;
	drag.axis = quader_editor_core::TransformAxis::y;
	drag.scene_transform_preview_eligible = true;
	drag.move = begin_axis_move(quader_editor_core::TransformAxis::y, {});

	quader_editor_core::TransformDragPreviewInput input;
	input.camera = front_camera();
	input.screen_position = { 100.0F, 100.0F };
	input.ray = { { 20.0F, 20.0F, 6.0F }, { 0.0F, 0.0F, -1.0F } };
	input.vertex_snap_enabled = true;
	input.vertex_snap_radius = 0.2;

	const quader_editor_core::CommandResult result =
		quader_editor_core::apply_transform_drag_preview(session, drag, input);

	EXPECT_TRUE(result.ok);
	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(drag.snap_target.kind == quader_editor_core::TransformSnapLandmarkKind::none);
	EXPECT_NEAR(drag.move.preview_delta.y, 0.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolMoveTests, TransformDragRuntimeHelperResetsStaleMoveStateBeforeActivation) {
	quader_editor_core::EditorRuntime runtime;
	quader_editor_core::TransformDragState stale_drag;
	stale_drag.active = true;
	stale_drag.axis = quader_editor_core::TransformAxis::x;
	stale_drag.move.preview_delta = { 99.0F, 0.0F, 0.0F };
	runtime.set_transform_drag_state(stale_drag);

	runtime.begin_transform_drag_state(
		quader_editor_core::ViewportInteractionTool::rotate,
		quader_editor_core::TransformDragOperation::transform,
		quader_editor_core::TransformAxis::z,
		{ 1.0F, 2.0F, 3.0F });

	EXPECT_TRUE(runtime.status().transform_drag_active);
	EXPECT_TRUE(runtime.state().transform_drag.tool == quader_editor_core::ViewportInteractionTool::rotate);
	EXPECT_TRUE(runtime.state().transform_drag.axis == quader_editor_core::TransformAxis::z);
	EXPECT_NEAR(runtime.state().transform_drag.pivot.x, 1.0F, 0.0001);
	EXPECT_NEAR(runtime.state().transform_drag.pivot.y, 2.0F, 0.0001);
	EXPECT_NEAR(runtime.state().transform_drag.pivot.z, 3.0F, 0.0001);
	EXPECT_NEAR(runtime.state().transform_drag.move.preview_delta.x, 0.0F, 0.0001);

	runtime.clear_transform_drag_state();
	EXPECT_FALSE(runtime.status().transform_drag_active);
}

TEST(QdrModelingRuntimeToolMoveTests, ModalMovePlannerRoutesDuplicatePreviewsAndToolCommits) {
	quader_editor_core::ModalMoveBeginPlan begin =
		quader_editor_core::plan_modal_move_drag_begin({ true, true, true });
	EXPECT_TRUE(begin.request_duplicate);
	EXPECT_TRUE(begin.preview_uses_transform);
	EXPECT_TRUE(begin.mark_selection_dirty);

	begin = quader_editor_core::plan_modal_move_drag_begin({ true, false, false });
	EXPECT_FALSE(begin.request_duplicate);
	EXPECT_FALSE(begin.preview_uses_transform);

	quader_editor_core::ModalMoveUpdatePlan update =
		quader_editor_core::plan_modal_move_drag_update({
			true,
			quader_editor_core::ModalMoveDragTool::move,
			true,
			{},
			{},
			quader_editor_core::tool_epsilon,
		});
	EXPECT_TRUE(update.handled);
	EXPECT_FALSE(update.changed);

	update = quader_editor_core::plan_modal_move_drag_update({
		true,
		quader_editor_core::ModalMoveDragTool::extend,
		false,
		{},
		{ 0.0F, 1.0F, 0.0F },
		quader_editor_core::tool_epsilon,
	});
	EXPECT_TRUE(update.route == quader_editor_core::ModalMovePreviewRoute::extend_patch_preview);

	update = quader_editor_core::plan_modal_move_drag_update({
		true,
		quader_editor_core::ModalMoveDragTool::extrude,
		false,
		{},
		{ 0.0F, 1.0F, 0.0F },
		quader_editor_core::tool_epsilon,
	});
	EXPECT_TRUE(update.route == quader_editor_core::ModalMovePreviewRoute::extrude_patch_preview);

	quader_editor_core::ModalMoveCommitPlan commit =
		quader_editor_core::plan_modal_move_drag_commit({
			true,
			quader_editor_core::ModalMoveDragTool::move,
			true,
			{},
			quader_editor_core::tool_epsilon,
		});
	EXPECT_TRUE(commit.handled);
	EXPECT_FALSE(commit.model_changed);
	EXPECT_TRUE(commit.restore_base_snapshot);
	EXPECT_TRUE(commit.restore_selection);

	commit = quader_editor_core::plan_modal_move_drag_commit({
		true,
		quader_editor_core::ModalMoveDragTool::extrude,
		false,
		{ 0.0F, 2.0F, 0.0F },
		quader_editor_core::tool_epsilon,
	});
	EXPECT_TRUE(commit.model_changed);
	EXPECT_TRUE(commit.route == quader_editor_core::ModalMoveCommitRoute::extrude_patch);
	EXPECT_TRUE(commit.undo_action == "Quader Extrude Face");
}

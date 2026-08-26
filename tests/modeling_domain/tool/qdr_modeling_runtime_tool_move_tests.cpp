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
#include <quader/modeling/neutral/polygon/document.hpp>

#include <vector>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
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
	for (quader_poly::Vertex& vertex : object.document.edit().vertices) {
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
        input.screen_tip = axis == quader_editor_core::TransformAxis::Y
                               ? quader::QVec2{100.0F, 200.0F}
                               : quader::QVec2{200.0F, 100.0F};
        input.screen_position = { 100.0F, 100.0F };
	const quader_editor_core::MoveBeginResult begin = quader_editor_core::begin_move_drag(input);
	EXPECT_TRUE(begin.ok);
	return begin.state;
}

quader_poly::Transform3 nonorthogonal_orientation()
{
	return {
		.x_axis = {1.0F, 0.0F, 0.0F},
		.y_axis = {0.6F, 0.8F, 0.0F},
		.z_axis = {0.6F, 0.0F, 0.8F},
	};
}

quader::QVec3 transformed_vector(
		const quader_poly::Transform3& transform,
		quader::QVec3 value)
{
	return {
		transform.x_axis.x * value.x + transform.y_axis.x * value.y +
			transform.z_axis.x * value.z,
		transform.x_axis.y * value.x + transform.y_axis.y * value.y +
			transform.z_axis.y * value.z,
		transform.x_axis.z * value.x + transform.y_axis.z * value.y +
			transform.z_axis.z * value.z,
	};
}

quader::QVec3 cross_vector(quader::QVec3 left, quader::QVec3 right)
{
	return {
		left.y * right.z - left.z * right.y,
		left.z * right.x - left.x * right.z,
		left.x * right.y - left.y * right.x,
	};
}

quader::QVec3 included_plane_normal(
		const quader_poly::Transform3& orientation,
		quader_editor_core::TransformAxis axis)
{
	if (axis == quader_editor_core::TransformAxis::Xy) {
		return cross_vector(orientation.x_axis, orientation.y_axis);
	}
	if (axis == quader_editor_core::TransformAxis::Xz) {
		return cross_vector(orientation.z_axis, orientation.x_axis);
	}
	return cross_vector(orientation.y_axis, orientation.z_axis);
}

quader_poly::Ray ray_through_plane_point(
		quader::QVec3 point,
		quader::QVec3 plane_normal)
{
	return {
		{
			point.x + plane_normal.x * 5.0F,
			point.y + plane_normal.y * 5.0F,
			point.z + plane_normal.z * 5.0F,
		},
		{-plane_normal.x, -plane_normal.y, -plane_normal.z},
	};
}

quader_editor_core::MoveToolState begin_plane_move(
		quader_editor_core::TransformAxis axis,
		const quader_poly::Transform3& orientation,
		quader::QVec3 center)
{
	const quader::QVec3 plane_normal = included_plane_normal(orientation, axis);
	quader_editor_core::MoveBeginInput input;
	input.axis = axis;
	input.orientation = orientation;
	input.center_local = center;
	input.has_world_center = true;
	input.world_center = center;
	input.screen_position = {100.0F, 100.0F};
	input.has_start_ray = true;
	input.start_ray = ray_through_plane_point(center, plane_normal);

	const quader_editor_core::MoveBeginResult begin =
		quader_editor_core::begin_move_drag(input);
	EXPECT_TRUE(begin.ok);
	return begin.state;
}

} // namespace

TEST(QdrModelingRuntimeToolMoveTests, MoveDragBeginsWithLocalAndScreenAxisState) {
	quader_editor_core::MoveBeginInput input;
        input.axis = quader_editor_core::TransformAxis::X;
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
        EXPECT_TRUE(begin.state.axis == quader_editor_core::TransformAxis::X);
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

TEST(QdrModelingRuntimeToolMoveTests,
     MoveDragProjectsAndSnapsPlaneMotionInTheProvidedOrientation) {
	quader_editor_core::MoveBeginInput input;
	input.axis = quader_editor_core::TransformAxis::Xy;
	input.orientation.x_axis = {0.0F, 1.0F, 0.0F};
	input.orientation.y_axis = {0.0F, 0.0F, 1.0F};
	input.orientation.z_axis = {1.0F, 0.0F, 0.0F};
	input.center_local = {};
	input.has_world_center = true;
	input.world_center = {};
	input.screen_position = {100.0F, 100.0F};
	input.has_start_ray = true;
	input.start_ray = {{5.0F, 0.25F, 0.25F}, {-1.0F, 0.0F, 0.0F}};

	const quader_editor_core::MoveBeginResult begin =
		quader_editor_core::begin_move_drag(input);
	ASSERT_TRUE(begin.ok);
	quader_editor_core::MoveToolState state = begin.state;
	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_viewport(
			state, {100.0F, 100.0F},
			{{5.0F, 1.45F, 2.35F}, {-1.0F, 0.0F, 0.0F}},
			front_camera(), false, true, 1.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_delta.x, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.y, 1.0F, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.z, 2.0F, 0.0001F);
}

TEST(QdrModelingRuntimeToolMoveTests,
     LocalXyPlaneMoveUsesTheFullNonorthogonalOrientationBasis) {
	const quader_poly::Transform3 orientation = nonorthogonal_orientation();
	quader_editor_core::MoveToolState state = begin_plane_move(
		quader_editor_core::TransformAxis::Xy, orientation, {});
	const quader::QVec3 expected =
		transformed_vector(orientation, {1.25F, -0.75F, 0.0F});

	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_viewport(
			state, {100.0F, 100.0F},
			ray_through_plane_point(
				expected,
				included_plane_normal(
					orientation, quader_editor_core::TransformAxis::Xy)),
			front_camera(), false, false, 1.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_delta.x, expected.x, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.y, expected.y, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.z, expected.z, 0.0001F);
}

TEST(QdrModelingRuntimeToolMoveTests,
     LocalXzPlaneMoveUsesTheFullNonorthogonalOrientationBasis) {
	const quader_poly::Transform3 orientation = nonorthogonal_orientation();
	quader_editor_core::MoveToolState state = begin_plane_move(
		quader_editor_core::TransformAxis::Xz, orientation, {});
	const quader::QVec3 expected =
		transformed_vector(orientation, {-0.5F, 0.0F, 1.5F});

	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_viewport(
			state, {100.0F, 100.0F},
			ray_through_plane_point(
				expected,
				included_plane_normal(
					orientation, quader_editor_core::TransformAxis::Xz)),
			front_camera(), false, false, 1.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_delta.x, expected.x, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.y, expected.y, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.z, expected.z, 0.0001F);
}

TEST(QdrModelingRuntimeToolMoveTests,
     LocalYzPlaneMoveUsesTheFullNonorthogonalOrientationBasis) {
	const quader_poly::Transform3 orientation = nonorthogonal_orientation();
	quader_editor_core::MoveToolState state = begin_plane_move(
		quader_editor_core::TransformAxis::Yz, orientation, {});
	const quader::QVec3 expected =
		transformed_vector(orientation, {0.0F, 0.75F, -1.25F});

	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_viewport(
			state, {100.0F, 100.0F},
			ray_through_plane_point(
				expected,
				included_plane_normal(
					orientation, quader_editor_core::TransformAxis::Yz)),
			front_camera(), false, false, 1.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_delta.x, expected.x, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.y, expected.y, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.z, expected.z, 0.0001F);
}

TEST(QdrModelingRuntimeToolMoveTests,
     LocalPlaneMoveSnapsTheCenterInNonorthogonalLogicalCoordinates) {
	const quader_poly::Transform3 orientation = nonorthogonal_orientation();
	const quader::QVec3 center =
		transformed_vector(orientation, {0.2F, -0.3F, 1.1F});
	quader_editor_core::MoveToolState state = begin_plane_move(
		quader_editor_core::TransformAxis::Xy, orientation, center);
	const quader::QVec3 raw_delta =
		transformed_vector(orientation, {0.55F, 0.9F, 0.0F});
	const quader::QVec3 target{
		center.x + raw_delta.x,
		center.y + raw_delta.y,
		center.z + raw_delta.z,
	};
	const quader::QVec3 expected =
		transformed_vector(orientation, {0.8F, 0.8F, 0.0F});

	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_viewport(
			state, {100.0F, 100.0F},
			ray_through_plane_point(
				target,
				included_plane_normal(
					orientation, quader_editor_core::TransformAxis::Xy)),
			front_camera(), false, true, 0.5, true);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_delta.x, expected.x, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.y, expected.y, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.z, expected.z, 0.0001F);
}

TEST(QdrModelingRuntimeToolMoveTests, ObjectMoveDragSnapsSelectionCenterToGridIntersections) {
  quader_editor_core::MoveToolState state = begin_axis_move(
      quader_editor_core::TransformAxis::X, {0.25F, 0.0F, 0.0F});

  const quader_editor_core::MovePreviewResult preview =
      quader_editor_core::preview_move_from_viewport(
          state, {164.0F, 100.0F}, {}, front_camera(), false, true, 1.0, true);

  EXPECT_TRUE(preview.ok);
  EXPECT_NEAR(preview.preview_delta.x, 0.75F, 0.0001);
  EXPECT_NEAR(state.center_world.x + preview.preview_delta.x, 1.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolMoveTests, MoveDragSnapsPreviewDeltaToHoveredVertexTarget) {
  quader_editor_core::MoveToolState state =
      begin_axis_move(quader_editor_core::TransformAxis::Y, {2.0F, 3.0F, 4.0F});

  const quader_editor_core::MovePreviewResult preview =
      quader_editor_core::preview_move_to_axis_snap_target(
          state, {99.0F, 7.0F, -30.0F});

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
        drag.tool = quader_editor_core::ViewportInteractionTool::Move;
        drag.operation = quader_editor_core::TransformDragOperation::Transform;
        drag.axis = quader_editor_core::TransformAxis::Y;
        drag.scene_transform_preview_eligible = true;
        drag.move = begin_axis_move(quader_editor_core::TransformAxis::Y, {});

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
        EXPECT_TRUE(drag.snap_target.kind ==
                    quader_editor_core::TransformSnapLandmarkKind::Vertex);
        EXPECT_TRUE(drag.snap_target.object_id == 2);
	EXPECT_NEAR(drag.move.preview_delta.x, 0.0F, 0.0001);
	EXPECT_NEAR(drag.move.preview_delta.y, 5.0F, 0.0001);
	EXPECT_NEAR(drag.move.preview_delta.z, 0.0F, 0.0001);

	const std::vector<quader_editor_core::SelectionOverlayPoint> points =
		quader_editor_core::transform_drag_snap_overlay_points(drag);
	EXPECT_TRUE(points.size() == 2U);
        EXPECT_TRUE(points[0].layer ==
                    quader_editor_core::SelectionOverlayLayer::HoverVertex);
        EXPECT_TRUE(points[0].object_id == 2);
        EXPECT_TRUE(
            points[0].color_abgr ==
            quader_editor_core::selection_overlay_style_token(
                quader_editor_core::SelectionOverlayStyleRole::HoverVertex));
        EXPECT_TRUE(
            points[1].layer ==
            quader_editor_core::SelectionOverlayLayer::SnapTargetVertex);
        EXPECT_TRUE(points[1].object_id == 2);
        EXPECT_TRUE(points[1].color_abgr ==
                    quader_editor_core::selection_overlay_style_token(
                        quader_editor_core::SelectionOverlayStyleRole::
                            SnapTargetVertex));
}

TEST(QdrModelingRuntimeToolMoveTests,
     MoveDragUsesWorldTransformedSnapParticipantsInsteadOfSessionObjects) {
  quader_editor_core::PolygonObject moving = cube_object(1, {}, true);
  quader_editor_core::PolygonObject local_target =
      cube_object(2, {20.0F, 0.0F, 0.0F}, false);
  quader_editor_core::EditorSession session({moving, local_target});
  session.set_selected_object_ids({1}, 1);

  quader_editor_core::PolygonObject world_target = local_target;
  world_target.id = 202;
  for (quader_poly::Vertex &vertex : world_target.document.edit().vertices) {
    vertex.position.x -= 20.0F;
    vertex.position.y += 4.0F;
  }
  std::vector<quader_editor_core::PolygonObject> world_snap_participants{
      moving, world_target};

  quader_editor_core::TransformDragState drag;
  drag.active = true;
  drag.tool = quader_editor_core::ViewportInteractionTool::Move;
  drag.operation = quader_editor_core::TransformDragOperation::Transform;
  drag.axis = quader_editor_core::TransformAxis::Y;
  drag.scene_transform_preview_eligible = true;
  drag.move = begin_axis_move(quader_editor_core::TransformAxis::Y, {});

  quader_editor_core::TransformDragPreviewInput input;
  input.camera = front_camera();
  input.screen_position = {100.0F, 100.0F};
  input.ray = {{1.0F, 5.0F, 6.0F}, {0.0F, 0.0F, -1.0F}};
  input.vertex_snap_enabled = true;
  input.vertex_snap_radius = 0.2;
  input.snap_objects = &world_snap_participants;

  const quader_editor_core::CommandResult result =
      quader_editor_core::apply_transform_drag_preview(session, drag, input);

  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(drag.snap_target.kind ==
              quader_editor_core::TransformSnapLandmarkKind::Vertex);
  EXPECT_EQ(drag.snap_target.object_id, 202);
  EXPECT_NEAR(drag.snap_target.position.x, 1.0F, 0.0001);
  EXPECT_NEAR(drag.snap_target.position.y, 5.0F, 0.0001);
  EXPECT_NEAR(drag.move.preview_delta.y, 5.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolMoveTests, MoveDragIgnoresVertexSnapWhenNoTargetIsInsideSnapRadius) {
	quader_editor_core::EditorSession session({ cube_object(1, {}, true) });
	session.set_selected_object_ids({ 1 }, 1);

	quader_editor_core::TransformDragState drag;
	drag.active = true;
        drag.tool = quader_editor_core::ViewportInteractionTool::Move;
        drag.operation = quader_editor_core::TransformDragOperation::Transform;
        drag.axis = quader_editor_core::TransformAxis::Y;
        drag.scene_transform_preview_eligible = true;
        drag.move = begin_axis_move(quader_editor_core::TransformAxis::Y, {});

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
        EXPECT_TRUE(drag.snap_target.kind ==
                    quader_editor_core::TransformSnapLandmarkKind::None);
        EXPECT_NEAR(drag.move.preview_delta.y, 0.0F, 0.0001);
}

TEST(QdrModelingRuntimeToolMoveTests, TransformDragRuntimeHelperResetsStaleMoveStateBeforeActivation) {
	quader_editor_core::EditorRuntime runtime;
	quader_editor_core::TransformDragState stale_drag;
	stale_drag.active = true;
        stale_drag.axis = quader_editor_core::TransformAxis::X;
        stale_drag.move.preview_delta = { 99.0F, 0.0F, 0.0F };
	runtime.set_transform_drag_state(stale_drag);

        runtime.begin_transform_drag_state(
            quader_editor_core::ViewportInteractionTool::Rotate,
            quader_editor_core::TransformDragOperation::Transform,
            quader_editor_core::TransformAxis::Z, {1.0F, 2.0F, 3.0F});

        EXPECT_TRUE(runtime.status().transform_drag_active);
        EXPECT_TRUE(runtime.state().transform_drag.tool ==
                    quader_editor_core::ViewportInteractionTool::Rotate);
        EXPECT_TRUE(runtime.state().transform_drag.axis ==
                    quader_editor_core::TransformAxis::Z);
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
                quader_editor_core::ModalMoveDragTool::Move,
                true,
                {},
                {},
                quader_editor_core::kToolEpsilon,
            });
        EXPECT_TRUE(update.handled);
	EXPECT_FALSE(update.changed);

        update = quader_editor_core::plan_modal_move_drag_update({
            true,
            quader_editor_core::ModalMoveDragTool::Extrude,
            false,
            {},
            {0.0F, 1.0F, 0.0F},
            quader_editor_core::kToolEpsilon,
        });
        EXPECT_TRUE(
            update.route ==
            quader_editor_core::ModalMovePreviewRoute::ExtrudePatchPreview);

        quader_editor_core::ModalMoveCommitPlan commit =
            quader_editor_core::plan_modal_move_drag_commit({
                true,
                quader_editor_core::ModalMoveDragTool::Move,
                true,
                {},
                quader_editor_core::kToolEpsilon,
            });
        EXPECT_TRUE(commit.handled);
	EXPECT_FALSE(commit.model_changed);
	EXPECT_TRUE(commit.restore_base_snapshot);
	EXPECT_TRUE(commit.restore_selection);

        commit = quader_editor_core::plan_modal_move_drag_commit({
            true,
            quader_editor_core::ModalMoveDragTool::Extrude,
            false,
            {0.0F, 2.0F, 0.0F},
            quader_editor_core::kToolEpsilon,
        });
        EXPECT_TRUE(commit.model_changed);
        EXPECT_TRUE(commit.route ==
                    quader_editor_core::ModalMoveCommitRoute::ExtrudePatch);
        EXPECT_TRUE(commit.undo_action == "Quader Extrude Face");
}

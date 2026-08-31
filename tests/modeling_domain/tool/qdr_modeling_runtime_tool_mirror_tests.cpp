////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/interaction_api.hpp>

#include <vector>

TEST(QdrModelingRuntimeToolMirrorTests, MirrorToolCapturesObjectTargetsFromCurrentSelection) {
	quader_editor_core::RectangleSelectionState selection;
        selection.mode = quader_editor_core::SelectionMode::Object;
        selection.selected_object_ids = { 4, 8 };
	selection.active_object_id = 4;

	quader_editor_core::MirrorToolState state;
	const quader_editor_core::MirrorToolResult first =
		quader_editor_core::capture_first_mirror_point(state, selection, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
	EXPECT_TRUE(first.ok);
	EXPECT_TRUE(first.changed);
	EXPECT_TRUE(first.target_count == 2);
	EXPECT_TRUE(state.target_object_ids.size() == 2U);
	EXPECT_TRUE(state.target_object_ids[0] == 4);
	EXPECT_TRUE(state.target_object_ids[1] == 8);
	EXPECT_TRUE(state.plane.dragging_new_point);
	EXPECT_TRUE(quader_editor_core::mirror_is_active(state));
	EXPECT_TRUE(quader_editor_core::mirror_is_dragging(state));
}

TEST(QdrModelingRuntimeToolMirrorTests, MirrorTargetPolicyIsObjectModeOnly) {
	quader_editor_core::RectangleSelectionState selection;
        selection.mode = quader_editor_core::SelectionMode::Face;
        selection.active_object_id = 5;
	selection.selected_faces = { { 5, 2 } };

	quader_editor_core::MirrorToolState state;
	const quader_editor_core::MirrorToolResult result =
		quader_editor_core::capture_first_mirror_point(state, selection, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(state.target_object_ids.empty());
	EXPECT_FALSE(quader_editor_core::mirror_can_preview(state));
}

TEST(QdrModelingRuntimeToolMirrorTests, MirrorToolUsesTwoSnappedWorldGridPoints) {
	quader_editor_core::RectangleSelectionState selection;
        selection.mode = quader_editor_core::SelectionMode::Object;
        selection.selected_object_ids = { 5 };
	selection.active_object_id = 5;

	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
        const quader_editor_core::CutGridPointResult grid_point =
		quader_editor_core::cut_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5);
	EXPECT_TRUE(grid_point.ok);

	quader_editor_core::MirrorToolState state;
	EXPECT_TRUE(quader_editor_core::capture_first_mirror_point(state, selection, grid_point.position, grid_point.plane_normal).ok);
	EXPECT_FALSE(quader_editor_core::capture_second_mirror_point(state, grid_point.position).ok);
	EXPECT_TRUE(quader_editor_core::capture_second_mirror_point(state, { 1.5F, 0.0F, 1.5F }).ok);

	EXPECT_TRUE(state.plane.points.size() == 2U);
	EXPECT_TRUE(state.target_object_ids.size() == 1U);
	EXPECT_TRUE(state.target_object_ids[0] == 5);
	EXPECT_TRUE(quader_editor_core::mirror_can_preview(state));
	const std::vector<quader::QVec3> plane_points = quader_editor_core::mirror_plane_points(state);
	EXPECT_TRUE(plane_points.size() == 3U);
	EXPECT_TRUE(plane_points[2].y > plane_points[0].y);
}

TEST(QdrModelingRuntimeToolMirrorTests, MirrorPreviewPolicyRetainsOnlyHostPlaneAndTargetState) {
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::MirrorToolState state;
	state.plane.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.plane.face_normal = { 0.0F, 0.0F, 1.0F };
	state.target_object_ids = { 7 };

	const quader_editor_core::MirrorToolResult result =
		quader_editor_core::refresh_mirror_preview(state, objects);
	EXPECT_TRUE(result.ok);

	const quader_editor_core::MirrorPreviewMetadata metadata =
		quader_editor_core::mirror_preview_metadata(state);
	EXPECT_TRUE(metadata.preview_valid);
	EXPECT_TRUE(metadata.target_count == 1);
	EXPECT_TRUE(metadata.mirrored_count == 1);
}

TEST(QdrModelingRuntimeToolMirrorTests, MirrorPreviewValidatesOnTheFirstDragAfterTheFirstPoint) {
	quader_editor_core::PolygonObject object;
	object.id = 7;
	object.document = quader_poly::make_cube({});
	const std::vector<quader_editor_core::PolygonObject> objects { object };

	quader_editor_core::RectangleSelectionState selection;
        selection.mode = quader_editor_core::SelectionMode::Object;
        selection.selected_object_ids = { 7 };
	selection.active_object_id = 7;

	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;

        quader_editor_core::EditorRuntime runtime;
	quader_editor_core::MirrorRayUpdateInput first_input;
	first_input.camera = camera;
	first_input.ray = { { 0.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	first_input.grid_size = 0.5;

	const quader_editor_core::MirrorToolResult first =
		runtime.click_mirror_from_ray(selection, objects, first_input);
	EXPECT_TRUE(first.ok);
	EXPECT_TRUE(first.changed);
	EXPECT_TRUE(runtime.mirror_state().plane.points.size() == 1U);
	EXPECT_TRUE(runtime.mirror_state().plane.dragging_new_point);
	EXPECT_FALSE(runtime.mirror_state().preview_valid);

	quader_editor_core::MirrorRayUpdateInput drag_input = first_input;
	drag_input.ray = { { 1.0F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } };
	drag_input.dragging = true;

	const quader_editor_core::MirrorToolResult drag =
		runtime.update_mirror_hover_from_ray(selection, objects, drag_input);
	EXPECT_TRUE(drag.ok);
	EXPECT_TRUE(drag.changed);
	EXPECT_TRUE(runtime.mirror_state().plane.points.size() == 2U);
	EXPECT_TRUE(runtime.mirror_state().plane.dragging_new_point);

	const quader_editor_core::MirrorPreviewMetadata metadata =
		quader_editor_core::mirror_preview_metadata(runtime.mirror_state());
	EXPECT_TRUE(metadata.can_preview);
	EXPECT_TRUE(metadata.preview_valid);
	EXPECT_TRUE(metadata.target_count == 1);

}

TEST(QdrModelingRuntimeToolMirrorTests, MirrorLineDragMovesTheWholePlacedPlane) {
	quader_editor_core::MirrorToolState state;
	state.plane.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	state.plane.face_normal = { 0.0F, 1.0F, 0.0F };
	state.target_object_ids = { 1 };
	state.preview_valid = true;

	EXPECT_TRUE(quader_editor_core::begin_mirror_line_drag(state, { 0.5F, 0.0F, 0.0F }).ok);
	EXPECT_TRUE(quader_editor_core::mirror_is_dragging(state));

	const quader_editor_core::MirrorToolResult moved =
		quader_editor_core::update_dragged_mirror_line(state, { 1.5F, 0.0F, 0.0F });
	EXPECT_TRUE(moved.ok);
	EXPECT_TRUE(moved.changed);
	EXPECT_TRUE(state.plane.points.size() == 2U);
	EXPECT_TRUE(state.plane.points[0].x == 1.0F);
	EXPECT_TRUE(state.plane.points[1].x == 2.0F);
	EXPECT_FALSE(state.preview_valid);

	EXPECT_TRUE(quader_editor_core::end_mirror_line_drag(state).ok);
	EXPECT_FALSE(quader_editor_core::mirror_is_dragging(state));
}

TEST(QdrModelingRuntimeToolMirrorTests, MirrorLineDragNormalizesEachAnchorToTheCurrentLine) {
	quader_editor_core::MirrorToolState state;
	state.plane.points = {
		{ 0.0F, 2.0F, 0.0F },
		{ 1.0F, 2.0F, 0.0F },
	};
	state.plane.face_normal = { 0.0F, 1.0F, 0.0F };
	state.target_object_ids = { 1 };
	state.preview_valid = true;

	ASSERT_TRUE(quader_editor_core::begin_mirror_line_drag(
		state, { 0.5F, 4.0F, 0.0F }).ok);
	EXPECT_FLOAT_EQ(state.plane.drag_line_anchor.x, 0.5F);
	EXPECT_FLOAT_EQ(state.plane.drag_line_anchor.y, 2.0F);
	ASSERT_TRUE(quader_editor_core::update_dragged_mirror_line(
		state, { 1.5F, 4.0F, 0.0F }).ok);
	ASSERT_TRUE(quader_editor_core::end_mirror_line_drag(state).ok);
	EXPECT_FLOAT_EQ(state.plane.points[0].x, 1.0F);
	EXPECT_FLOAT_EQ(state.plane.points[0].y, 4.0F);
	EXPECT_FLOAT_EQ(state.plane.points[1].x, 2.0F);
	EXPECT_FLOAT_EQ(state.plane.points[1].y, 4.0F);

	ASSERT_TRUE(quader_editor_core::begin_mirror_line_drag(
		state, { 1.5F, 6.0F, 0.0F }).ok);
	EXPECT_FLOAT_EQ(state.plane.drag_line_anchor.x, 1.5F);
	EXPECT_FLOAT_EQ(state.plane.drag_line_anchor.y, 4.0F);
	ASSERT_TRUE(quader_editor_core::update_dragged_mirror_line(
		state, { 2.5F, 6.0F, 0.0F }).ok);
	ASSERT_TRUE(quader_editor_core::end_mirror_line_drag(state).ok);
	EXPECT_FLOAT_EQ(state.plane.points[0].x, 2.0F);
	EXPECT_FLOAT_EQ(state.plane.points[0].y, 6.0F);
	EXPECT_FLOAT_EQ(state.plane.points[1].x, 3.0F);
	EXPECT_FLOAT_EQ(state.plane.points[1].y, 6.0F);
}

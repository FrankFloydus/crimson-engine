////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

TEST(QdrModelingRuntimeToolRotateTests, RotateDragSnapsScreenAngleAndBuildsPivotedTransform) {
	quader_editor_core::RotateBeginInput input;
	input.axis = quader_editor_core::TransformAxis::z;
	input.object_ids = { 1, 2 };
	input.pivot = { 1.0F, 2.0F, 0.0F };
	input.screen_position = { 120.0F, 100.0F };
	input.has_screen_pivot = true;
	input.screen_pivot = { 100.0F, 100.0F };

	const quader_editor_core::RotateBeginResult begin = quader_editor_core::begin_rotate_drag(input);
	EXPECT_TRUE(begin.ok);
	quader_editor_core::RotateToolState state = begin.state;

	const quader_editor_core::RotatePreviewResult preview =
		quader_editor_core::preview_rotate_from_screen_position(state, { 100.0F, 80.0F }, 15.0);
	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_angle_degrees, 90.0, 0.0001);
	EXPECT_NEAR(preview.transform.x_axis.x, 0.0, 0.0001);
	EXPECT_NEAR(preview.transform.x_axis.y, 1.0, 0.0001);
	EXPECT_FALSE(qdr_test::nearly_equal(preview.pivoted_transform.origin.x, 0.0F, 0.0001));
}

TEST(QdrModelingRuntimeToolRotateTests, RotateDragAccumulatesCircularMotionBeyondHalfTurns) {
	quader_editor_core::RotateBeginInput input;
	input.axis = quader_editor_core::TransformAxis::z;
	input.screen_position = { 120.0F, 100.0F };
	input.has_screen_pivot = true;
	input.screen_pivot = { 100.0F, 100.0F };

	const quader_editor_core::RotateBeginResult begin = quader_editor_core::begin_rotate_drag(input);
	EXPECT_TRUE(begin.ok);
	quader_editor_core::RotateToolState state = begin.state;

	quader_editor_core::RotatePreviewResult preview =
		quader_editor_core::preview_rotate_from_screen_position(state, { 100.0F, 80.0F }, 0.0);
	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_angle_degrees, 90.0, 0.0001);

	preview = quader_editor_core::preview_rotate_from_screen_position(state, { 80.0F, 100.0F }, 0.0);
	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_angle_degrees, 180.0, 0.0001);

	preview = quader_editor_core::preview_rotate_from_screen_position(state, { 100.0F, 120.0F }, 0.0);
	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_angle_degrees, 270.0, 0.0001);
}

TEST(QdrModelingRuntimeToolRotateTests, RotateDragUsesLinearSensitivityWhenAxisIsEdgeOn) {
	quader_editor_core::RotateBeginInput input;
	input.axis = quader_editor_core::TransformAxis::x;
	input.screen_position = { 100.0F, 100.0F };
	input.has_screen_pivot = true;
	input.screen_pivot = { 100.0F, 100.0F };
	input.has_screen_tip = true;
	input.screen_tip = { 100.0F, 20.0F };
	input.has_world_direction = true;
	input.world_direction = { 1.0F, 0.0F, 0.0F };
	input.has_world_pivot = true;
	input.world_pivot = {};
	input.has_camera = true;
	input.camera_transform.origin = { 0.0F, 0.0F, 6.0F };
	input.camera_transform.x_axis = { 1.0F, 0.0F, 0.0F };
	input.camera_transform.y_axis = { 0.0F, 1.0F, 0.0F };
	input.camera_transform.z_axis = { 0.0F, 0.0F, 1.0F };

	const quader_editor_core::RotateBeginResult begin = quader_editor_core::begin_rotate_drag(input);
	EXPECT_TRUE(begin.ok);
	quader_editor_core::RotateToolState state = begin.state;
	EXPECT_TRUE(state.edge_on_linear_drag);

	const quader_editor_core::RotatePreviewResult preview =
		quader_editor_core::preview_rotate_from_screen_position(state, { 100.0F, 180.0F }, 0.0);
	EXPECT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_angle_degrees, 90.0, 0.0001);
}

TEST(QdrModelingRuntimeToolRotateTests, ModalRotatePlannerClearsTinyDuplicatePreviews) {
	quader_editor_core::ModalTransformBeginPlan begin =
		quader_editor_core::plan_modal_transform_drag_begin({ true, false });
	EXPECT_FALSE(begin.ok);
	EXPECT_FALSE(begin.request_duplicate);

	begin = quader_editor_core::plan_modal_transform_drag_begin({ true, true });
	EXPECT_TRUE(begin.ok);
	EXPECT_TRUE(begin.request_duplicate);
	EXPECT_TRUE(begin.mark_selection_dirty);

	const quader_editor_core::ModalTransformUpdatePlan update =
		quader_editor_core::plan_modal_transform_drag_update({
			true,
			quader_editor_core::ModalTransformDragTool::rotate,
			true,
			0.0,
			{ 1.0F, 1.0F, 1.0F },
			quader_editor_core::tool_epsilon,
		});
	EXPECT_TRUE(update.handled);
	EXPECT_TRUE(update.preview_ok);
	EXPECT_FALSE(update.preview_valid);
	EXPECT_TRUE(update.route == quader_editor_core::ModalTransformPreviewRoute::clear_preview);

	const quader_editor_core::ModalTransformCommitPlan commit =
		quader_editor_core::plan_modal_transform_drag_commit({
			true,
			quader_editor_core::ModalTransformDragTool::rotate,
			false,
			true,
		});
	EXPECT_TRUE(commit.handled);
	EXPECT_TRUE(commit.restore_base_snapshot);
	EXPECT_TRUE(commit.restore_selection);
	EXPECT_FALSE(commit.model_changed);
}

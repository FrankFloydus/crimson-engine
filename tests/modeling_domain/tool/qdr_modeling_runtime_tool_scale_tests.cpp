////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

#include <utility>

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

} // namespace

TEST(QdrModelingRuntimeToolScaleTests, ScaleDragSnapsAgainstBoundsAndSupportsUniformCenterPick) {
	quader_editor_core::ScaleBeginInput input;
        input.axis = quader_editor_core::TransformAxis::X;
        input.pivot = { 0.0F, 0.0F, 0.0F };
	input.screen_position = { 200.0F, 200.0F };
	input.has_screen_pivot = true;
	input.screen_pivot = { 200.0F, 200.0F };
	input.has_screen_tip = true;
	input.screen_tip = { 260.0F, 200.0F };
	input.bounds.has_bounds = true;
	input.bounds.min = { -1.0F, -1.0F, -1.0F };
	input.bounds.max = { 1.0F, 1.0F, 1.0F };

	const quader_editor_core::ScaleBeginResult begin = quader_editor_core::begin_scale_drag(input);
	EXPECT_TRUE(begin.ok);
	quader_editor_core::ScaleToolState state = begin.state;

	const quader_editor_core::ScalePreviewResult preview =
		quader_editor_core::preview_scale_from_screen_position(state, { 300.0F, 200.0F }, true, 0.5);
	EXPECT_TRUE(preview.ok);
	EXPECT_TRUE(preview.preview_scale.x > 1.0F);
	EXPECT_NEAR(preview.preview_scale.y, 1.0F, 0.0001);
	EXPECT_NEAR(preview.preview_scale.z, 1.0F, 0.0001);

	quader_editor_core::TransformGizmoFrameInput gizmo;
        gizmo.tool = quader_editor_core::ViewportInteractionTool::Scale;
        gizmo.has_selection = true;
	gizmo.has_pivot = true;
	gizmo.pivot = {};
	gizmo.camera = front_camera();
	gizmo.gizmo_size_pixels = 72.0;

	const quader_editor_core::TransformGizmoPickHit center_hit =
		quader_editor_core::pick_transform_gizmo(gizmo, { 400.0F, 300.0F });
	EXPECT_TRUE(center_hit.hit);
        EXPECT_TRUE(center_hit.axis == quader_editor_core::TransformAxis::All);
}

TEST(QdrModelingRuntimeToolScaleTests,
     UniformCenterScaleCanShrinkGridSizedBoundsBelowOneGridCell) {
	quader_editor_core::ScaleBeginInput input;
	input.axis = quader_editor_core::TransformAxis::All;
	input.pivot = {};
	input.screen_position = {200.0F, 200.0F};
	input.has_screen_pivot = true;
	input.screen_pivot = input.screen_position;
	input.has_screen_tip = true;
	input.screen_tip = {296.0F, 200.0F};
	input.bounds.has_bounds = true;
	input.bounds.min = {-0.5F, -0.5F, -0.5F};
	input.bounds.max = {0.5F, 0.5F, 0.5F};

	const quader_editor_core::ScaleBeginResult begin =
		quader_editor_core::begin_scale_drag(input);
	ASSERT_TRUE(begin.ok);
	quader_editor_core::ScaleToolState state = begin.state;
	const quader_editor_core::ScalePreviewResult preview =
		quader_editor_core::preview_scale_from_screen_position(
			state, {152.0F, 200.0F}, true, 1.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_scale.x, 0.5F, 0.0001F);
	EXPECT_NEAR(preview.preview_scale.y, 0.5F, 0.0001F);
	EXPECT_NEAR(preview.preview_scale.z, 0.5F, 0.0001F);
	EXPECT_NEAR(preview.transform.x_axis.x, 0.5F, 0.0001F);
	EXPECT_NEAR(preview.transform.y_axis.y, 0.5F, 0.0001F);
	EXPECT_NEAR(preview.transform.z_axis.z, 0.5F, 0.0001F);
}

TEST(QdrModelingRuntimeToolScaleTests,
     OutwardUniformGridSnapNeverShrinksNonAlignedBounds) {
	quader_editor_core::ScaleBeginInput input;
	input.axis = quader_editor_core::TransformAxis::All;
	input.pivot = {};
	input.screen_position = {200.0F, 200.0F};
	input.has_screen_pivot = true;
	input.screen_pivot = input.screen_position;
	input.has_screen_tip = true;
	input.screen_tip = {296.0F, 200.0F};
	input.bounds.has_bounds = true;
	input.bounds.min = {-1.6F, -1.6F, -1.6F};
	input.bounds.max = {1.6F, 1.6F, 1.6F};

	const quader_editor_core::ScaleBeginResult begin =
		quader_editor_core::begin_scale_drag(input);
	ASSERT_TRUE(begin.ok);
	quader_editor_core::ScaleToolState state = begin.state;
	const quader_editor_core::ScalePreviewResult preview =
		quader_editor_core::preview_scale_from_screen_position(
			state, {201.0F, 200.0F}, true, 1.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_GE(preview.preview_scale.x, 1.0F);
	EXPECT_GE(preview.preview_scale.y, 1.0F);
	EXPECT_GE(preview.preview_scale.z, 1.0F);
}

TEST(QdrModelingRuntimeToolScaleTests,
     ScaleDragConjugatesTheAxisDeltaThroughItsOrientation) {
	quader_editor_core::ScaleBeginInput input;
	input.axis = quader_editor_core::TransformAxis::X;
	input.orientation.x_axis = {0.0F, 1.0F, 0.0F};
	input.orientation.y_axis = {-1.0F, 0.0F, 0.0F};
	input.pivot = {};
	input.screen_position = {200.0F, 200.0F};

	const quader_editor_core::ScaleBeginResult begin =
		quader_editor_core::begin_scale_drag(input);
	ASSERT_TRUE(begin.ok);
	quader_editor_core::ScaleToolState state = begin.state;
	const quader_editor_core::ScalePreviewResult preview =
		quader_editor_core::preview_scale_from_scale(
			state, {2.0F, 1.0F, 1.0F});

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.transform.x_axis.x, 1.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.x_axis.y, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.y_axis.x, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.y_axis.y, 2.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.z_axis.z, 1.0F, 0.0001F);
}

TEST(QdrModelingRuntimeToolScaleTests,
     ComponentScaleChoosesNearestEndpointGridFactorWithIdentityTies) {
	struct ScaleCase {
		double raw_factor;
		float expected_factor;
	};
	const ScaleCase cases[] = {
		{1.2, 1.25F}, {0.8, 0.75F}, {1.125, 1.0F}, {0.875, 1.0F},
		{0.1, 0.0F}, {1.0, 1.0F}, {1.2, 1.25F}, {1.0, 1.0F},
	};
	for (const bool mirrored : {false, true}) {
		quader_editor_core::ScaleBeginInput input;
		input.axis = quader_editor_core::TransformAxis::X;
		input.pivot = {mirrored ? -5.0F : 5.0F, 0.0F, 0.0F};
		input.screen_position = {200.0F, 200.0F};
		input.bounds.has_bounds = true;
		input.bounds.min = {mirrored ? -9.0F : 1.0F, -2.0F, -2.0F};
		input.bounds.max = {mirrored ? -1.0F : 9.0F, 2.0F, 2.0F};
		input.snap_bounds_to_grid = true;
		const auto begin = quader_editor_core::begin_scale_drag(input);
		ASSERT_TRUE(begin.ok);
		auto state = begin.state;
		for (const ScaleCase& scale_case : cases) {
			SCOPED_TRACE(mirrored);
			SCOPED_TRACE(scale_case.raw_factor);
			const quader::QVec2 pointer{
				static_cast<float>(200.0 + (scale_case.raw_factor - 1.0) * 96.0), 200.0F};
			const auto preview = quader_editor_core::preview_scale_from_screen_position(
				state, pointer, true, 8.0);
			ASSERT_TRUE(preview.ok);
			EXPECT_NEAR(preview.preview_scale.x, scale_case.expected_factor, 0.0001F);
			EXPECT_FLOAT_EQ(preview.preview_scale.y, 1.0F);
			EXPECT_FLOAT_EQ(preview.preview_scale.z, 1.0F);
			const auto unsnapped = quader_editor_core::preview_scale_from_screen_position(
				state, pointer, false, 8.0);
			ASSERT_TRUE(unsnapped.ok);
			EXPECT_NEAR(unsnapped.preview_scale.x, scale_case.raw_factor, 0.0001);
		}
	}
}

TEST(QdrModelingRuntimeToolScaleTests,
     ComponentPlaneScaleUsesOneFactorAcrossCapturedLogicalAxes) {
	quader_editor_core::ScaleBeginInput input;
	input.axis = quader_editor_core::TransformAxis::Xy;
	input.orientation = {
		{-1.0F, 0.0F, 0.0F}, {0.5F, 1.0F, 0.0F}, {0.25F, 0.0F, 1.0F}, {}};
	input.pivot = quader_editor_core::transform_vector(input.orientation, {5.0F, 8.0F, 2.0F});
	input.screen_position = {200.0F, 200.0F};
	input.bounds.has_bounds = true;
	input.bounds.min = {1.0F, 2.0F, 1.0F};
	input.bounds.max = {9.0F, 14.0F, 3.0F};
	input.snap_bounds_to_grid = true;
	const auto begin = quader_editor_core::begin_scale_drag(input);
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	for (const auto& sample : {std::pair{1.2, 1.25}, std::pair{1.3, 4.0 / 3.0}}) {
		const auto preview = quader_editor_core::preview_scale_from_screen_position(
			state, {static_cast<float>(200.0 + (sample.first - 1.0) * 96.0), 200.0F}, true, 8.0);
		ASSERT_TRUE(preview.ok);
		EXPECT_NEAR(preview.preview_scale.x, sample.second, 0.0001);
		EXPECT_NEAR(preview.preview_scale.y, sample.second, 0.0001);
		EXPECT_FLOAT_EQ(preview.preview_scale.z, 1.0F);
		const auto transformed_pivot = quader_editor_core::transform_point(preview.pivoted_transform, input.pivot);
		EXPECT_NEAR(transformed_pivot.x, input.pivot.x, 0.0001F);
		EXPECT_NEAR(transformed_pivot.y, input.pivot.y, 0.0001F);
		EXPECT_NEAR(transformed_pivot.z, input.pivot.z, 0.0001F);
	}
}

TEST(QdrModelingRuntimeToolScaleTests,
     OrdinaryObjectConstrainedScaleRetainsDimensionsBasedGridSnapping) {
	quader_editor_core::ScaleBeginInput input;
	input.axis = quader_editor_core::TransformAxis::X;
	input.pivot = {5.0F, 0.0F, 0.0F};
	input.screen_position = {200.0F, 200.0F};
	input.bounds.has_bounds = true;
	input.bounds.min = {1.0F, -2.0F, -2.0F};
	input.bounds.max = {9.0F, 2.0F, 2.0F};
	const auto begin = quader_editor_core::begin_scale_drag(input);
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	const auto preview = quader_editor_core::preview_scale_from_screen_position(
		state, {219.2F, 200.0F}, true, 8.0);
	ASSERT_TRUE(preview.ok);
	EXPECT_FLOAT_EQ(preview.preview_scale.x, 1.0F);
	EXPECT_FLOAT_EQ(preview.preview_scale.y, 1.0F);
	EXPECT_FLOAT_EQ(preview.preview_scale.z, 1.0F);
}

TEST(QdrModelingRuntimeToolScaleTests,
     ComponentScaleIgnoresNearlyCoincidentPivotEndpoints) {
	struct EndpointCase {
		float extent;
		float expected_factor;
	};
	for (const EndpointCase& endpoint_case : {
		EndpointCase{0.0F, 1.0F}, EndpointCase{0.0008F, 1.0F}, EndpointCase{0.001F, 2.0F}}) {
		SCOPED_TRACE(endpoint_case.extent);
		quader_editor_core::ScaleBeginInput input;
		input.axis = quader_editor_core::TransformAxis::X;
		input.screen_position = {200.0F, 200.0F};
		input.bounds.has_bounds = true;
		input.bounds.min = {};
		input.bounds.max = {endpoint_case.extent, 1.0F, 1.0F};
		input.snap_bounds_to_grid = true;
		const auto begin = quader_editor_core::begin_scale_drag(input);
		ASSERT_TRUE(begin.ok);
		auto state = begin.state;
		const auto preview = quader_editor_core::preview_scale_from_screen_position(
			state, {257.6F, 200.0F}, true, 0.001);
		ASSERT_TRUE(preview.ok);
		EXPECT_NEAR(preview.preview_scale.x, endpoint_case.expected_factor, 0.0001F);
		EXPECT_FLOAT_EQ(preview.preview_scale.y, 1.0F);
		EXPECT_FLOAT_EQ(preview.preview_scale.z, 1.0F);
	}
}

TEST(QdrModelingRuntimeToolScaleTests,
     UniformScaleRemainsFreeAcrossGridChangesForObjectsAndComponents) {
	for (const bool snap_bounds_to_grid : {false, true}) {
		quader_editor_core::ScaleBeginInput input;
		input.axis = quader_editor_core::TransformAxis::All;
		input.pivot = {5.0F, 5.0F, 5.0F};
		input.screen_position = {200.0F, 200.0F};
		input.bounds.has_bounds = true;
		input.bounds.min = {1.0F, 1.0F, 1.0F};
		input.bounds.max = {9.0F, 9.0F, 9.0F};
		input.snap_bounds_to_grid = snap_bounds_to_grid;
		const auto begin = quader_editor_core::begin_scale_drag(input);
		ASSERT_TRUE(begin.ok);
		auto state = begin.state;
		for (const double grid_size : {8.0, 1.0, 32.0}) {
			for (const bool grid_enabled : {true, false}) {
				const auto preview = quader_editor_core::preview_scale_from_screen_position(
					state, {219.2F, 200.0F}, grid_enabled, grid_size);
				ASSERT_TRUE(preview.ok);
				EXPECT_NEAR(preview.preview_scale.x, 1.2F, 0.0001F);
				EXPECT_NEAR(preview.preview_scale.y, 1.2F, 0.0001F);
				EXPECT_NEAR(preview.preview_scale.z, 1.2F, 0.0001F);
			}
		}
	}
}

TEST(QdrModelingRuntimeToolScaleTests,
     UniformTopologyScaleRetainsDimensionsSnapWithoutChangingOrdinaryScale) {
	for (const bool snap_uniform_to_grid : {false, true}) {
		quader_editor_core::ScaleBeginInput input;
		input.axis = quader_editor_core::TransformAxis::All;
		input.screen_position = {200.0F, 200.0F};
		input.bounds.has_bounds = true;
		input.bounds.min = {-1.0F, -1.0F, -1.0F};
		input.bounds.max = {1.0F, 1.0F, 1.0F};
		input.snap_uniform_to_grid = snap_uniform_to_grid;
		const auto begin = quader_editor_core::begin_scale_drag(input);
		ASSERT_TRUE(begin.ok);
		auto state = begin.state;
		for (const bool grid_enabled : {true, false, true}) {
			const auto preview = quader_editor_core::preview_scale_from_screen_position(
				state, {180.8F, 200.0F}, grid_enabled, 0.5);
			ASSERT_TRUE(preview.ok);
			const float expected = snap_uniform_to_grid && grid_enabled ? 0.75F : 0.8F;
			EXPECT_NEAR(preview.preview_scale.x, expected, 0.0001F);
			EXPECT_NEAR(preview.preview_scale.y, expected, 0.0001F);
			EXPECT_NEAR(preview.preview_scale.z, expected, 0.0001F);
		}
	}
}

TEST(QdrModelingRuntimeToolScaleTests, ModalScalePlannerRoutesNonIdentityPreviewsAndCommitNames) {
  const quader_editor_core::ModalTransformUpdatePlan update =
      quader_editor_core::plan_modal_transform_drag_update({
          true,
          quader_editor_core::ModalTransformDragTool::Scale,
          true,
          0.0,
          {1.0F, 1.25F, 1.0F},
          quader_editor_core::kToolEpsilon,
      });
  EXPECT_TRUE(update.handled);
  EXPECT_TRUE(update.preview_valid);
  EXPECT_TRUE(update.route ==
              quader_editor_core::ModalTransformPreviewRoute::TransformPreview);

  const quader_editor_core::ModalTransformCommitPlan commit =
      quader_editor_core::plan_modal_transform_drag_commit({
          true,
          quader_editor_core::ModalTransformDragTool::Scale,
          true,
          false,
      });
  EXPECT_TRUE(commit.model_changed);
  EXPECT_TRUE(commit.route ==
              quader_editor_core::ModalTransformCommitRoute::TransformPatch);
  EXPECT_TRUE(commit.undo_action == "Quader Scale Brushes");
}

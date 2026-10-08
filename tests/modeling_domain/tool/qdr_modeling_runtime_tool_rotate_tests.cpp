////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

#include <limits>

namespace {

quader_editor_core::RotateBeginInput trackball_begin_input()
{
	quader_editor_core::RotateBeginInput input;
	input.axis = quader_editor_core::TransformAxis::All;
	input.has_camera = true;
	input.camera_transform.origin = { 0.0F, 0.0F, 6.0F };
	input.screen_position = { 200.0F, 150.0F };
	input.pivot = { 2.0F, 3.0F, 4.0F };
	return input;
}

void expect_vector_near(quader::QVec3 actual, quader::QVec3 expected)
{
	EXPECT_NEAR(actual.x, expected.x, 0.00001F);
	EXPECT_NEAR(actual.y, expected.y, 0.00001F);
	EXPECT_NEAR(actual.z, expected.z, 0.00001F);
}

void expect_transform_near(
	const quader_poly::Transform3& actual, const quader_poly::Transform3& expected)
{
	expect_vector_near(actual.x_axis, expected.x_axis);
	expect_vector_near(actual.y_axis, expected.y_axis);
	expect_vector_near(actual.z_axis, expected.z_axis);
	expect_vector_near(actual.origin, expected.origin);
}

} // namespace

TEST(QdrModelingRuntimeToolRotateTests, TrackballUsesCameraAxesForHorizontalAndVerticalMotion)
{
	const auto input = trackball_begin_input();
	const auto begin = quader_editor_core::begin_rotate_drag(input);
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	const auto horizontal = quader_editor_core::preview_rotate_from_screen_position(
		state, { 300.0F, 150.0F }, 0.0);
	ASSERT_TRUE(horizontal.ok);
	expect_vector_near(horizontal.transform.z_axis,
		{ static_cast<float>(std::sin(1.0)), 0.0F, static_cast<float>(std::cos(1.0)) });
	expect_vector_near(horizontal.transform.y_axis, { 0.0F, 1.0F, 0.0F });
	expect_vector_near(quader_editor_core::transform_point(horizontal.pivoted_transform, input.pivot), input.pivot);
	expect_transform_near(quader_editor_core::rotate_preview_transform(state), horizontal.transform);

	state = begin.state;
	const auto vertical = quader_editor_core::preview_rotate_from_screen_position(
		state, { 200.0F, 250.0F }, 0.0);
	ASSERT_TRUE(vertical.ok);
	expect_vector_near(vertical.transform.z_axis,
		{ 0.0F, static_cast<float>(-std::sin(1.0)), static_cast<float>(std::cos(1.0)) });
	expect_vector_near(vertical.transform.x_axis, { 1.0F, 0.0F, 0.0F });

	state = begin.state;
	const auto left = quader_editor_core::preview_rotate_from_screen_position(
		state, { 100.0F, 150.0F }, 0.0);
	ASSERT_TRUE(left.ok);
	expect_vector_near(left.transform.z_axis,
		{ static_cast<float>(-std::sin(1.0)), 0.0F, static_cast<float>(std::cos(1.0)) });
}

TEST(QdrModelingRuntimeToolRotateTests, TrackballDiagonalMotionUsesOneCombinedRotation)
{
	const auto begin = quader_editor_core::begin_rotate_drag(trackball_begin_input());
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	const auto diagonal = quader_editor_core::preview_rotate_from_screen_position(
		state, { 250.0F, 200.0F }, 0.0);
	ASSERT_TRUE(diagonal.ok);
	const double angle = std::sqrt(0.5);
	const float projected_sine = static_cast<float>(std::sin(angle) / std::sqrt(2.0));
	expect_vector_near(diagonal.transform.z_axis,
		{ projected_sine, -projected_sine, static_cast<float>(std::cos(angle)) });
	EXPECT_NEAR(diagonal.preview_angle_degrees, angle * 180.0 / std::acos(-1.0), 0.00001);

	const auto returned = quader_editor_core::preview_rotate_from_screen_position(
		state, { 200.0F, 150.0F }, 0.0);
	ASSERT_TRUE(returned.ok);
	expect_transform_near(returned.transform, {});
	expect_transform_near(returned.pivoted_transform, {});
	EXPECT_NEAR(returned.preview_angle_degrees, 0.0, 0.00001);
}

TEST(QdrModelingRuntimeToolRotateTests, TrackballFreezesCameraFrameAndIgnoresGizmoOrientation)
{
	auto input = trackball_begin_input();
	input.camera_transform.x_axis = { 0.0F, 0.0F, -2.0F };
	input.camera_transform.y_axis = { 0.0F, 3.0F, 0.0F };
	input.camera_transform.z_axis = { 2.0F, 0.0F, 0.0F };
	input.orientation.x_axis = { 0.0F, 1.0F, 0.0F };
	input.orientation.y_axis = { -1.0F, 0.0F, 0.0F };
	const auto begin = quader_editor_core::begin_rotate_drag(input);
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	input.camera_transform = {};
	const auto preview = quader_editor_core::preview_rotate_from_screen_position(
		state, { 200.0F, 250.0F }, 0.0);
	ASSERT_TRUE(preview.ok);
	expect_vector_near(preview.transform.x_axis,
		{ static_cast<float>(std::cos(1.0)), static_cast<float>(-std::sin(1.0)), 0.0F });
	expect_vector_near(preview.transform.z_axis, { 0.0F, 0.0F, 1.0F });
}

TEST(QdrModelingRuntimeToolRotateTests, TrackballSensitivityDoesNotDependOnPressPointOrPivot)
{
	const auto first = quader_editor_core::begin_rotate_drag(trackball_begin_input());
	ASSERT_TRUE(first.ok);
	auto shifted_input = trackball_begin_input();
	shifted_input.screen_position = { 600.0F, 400.0F };
	shifted_input.has_screen_pivot = true;
	shifted_input.screen_pivot = { 550.0F, 420.0F };
	const auto shifted = quader_editor_core::begin_rotate_drag(shifted_input);
	ASSERT_TRUE(shifted.ok);
	auto first_state = first.state;
	auto shifted_state = shifted.state;
	const auto first_preview = quader_editor_core::preview_rotate_from_screen_position(
		first_state, { 160.0F, 190.0F }, 0.0);
	const auto shifted_preview = quader_editor_core::preview_rotate_from_screen_position(
		shifted_state, { 560.0F, 440.0F }, 0.0);
	ASSERT_TRUE(first_preview.ok);
	ASSERT_TRUE(shifted_preview.ok);
	expect_transform_near(first_preview.transform, shifted_preview.transform);
}

TEST(QdrModelingRuntimeToolRotateTests, TrackballPrecisionChangesOnlyNewMotionWithoutJumps)
{
	const auto begin = quader_editor_core::begin_rotate_drag(trackball_begin_input());
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	const auto full_speed = quader_editor_core::preview_rotate_from_screen_position(
		state, { 300.0F, 150.0F }, 0.0);
	ASSERT_TRUE(full_speed.ok);
	const auto toggle_on = quader_editor_core::preview_rotate_from_screen_position(
		state, { 300.0F, 150.0F }, 0.0, true);
	ASSERT_TRUE(toggle_on.ok);
	expect_transform_near(toggle_on.transform, full_speed.transform);
	const auto precise = quader_editor_core::preview_rotate_from_screen_position(
		state, { 340.0F, 150.0F }, 0.0, true);
	ASSERT_TRUE(precise.ok);
	expect_vector_near(precise.transform.z_axis,
		{ static_cast<float>(std::sin(1.1)), 0.0F, static_cast<float>(std::cos(1.1)) });
	const auto toggle_off = quader_editor_core::preview_rotate_from_screen_position(
		state, { 340.0F, 150.0F }, 0.0, false);
	ASSERT_TRUE(toggle_off.ok);
	expect_transform_near(toggle_off.transform, precise.transform);
	const auto reverse_precise = quader_editor_core::preview_rotate_from_screen_position(
		state, { 300.0F, 150.0F }, 0.0, true);
	ASSERT_TRUE(reverse_precise.ok);
	expect_transform_near(reverse_precise.transform, full_speed.transform);
	const auto returned = quader_editor_core::preview_rotate_from_screen_position(
		state, { 200.0F, 150.0F }, 0.0, false);
	ASSERT_TRUE(returned.ok);
	expect_transform_near(returned.transform, {});
}

TEST(QdrModelingRuntimeToolRotateTests, TrackballRemainsFreeWhenAngularSnapChanges)
{
	const auto begin = quader_editor_core::begin_rotate_drag(trackball_begin_input());
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	const auto preview = quader_editor_core::preview_rotate_from_screen_position(
		state, { 240.0F, 170.0F }, 15.0);
	ASSERT_TRUE(preview.ok);
	const double angle = std::sqrt(0.2);
	const double component = std::sin(angle) / std::sqrt(5.0);
	expect_vector_near(preview.transform.z_axis,
		{ static_cast<float>(2.0 * component), static_cast<float>(-component), static_cast<float>(std::cos(angle)) });
	EXPECT_NEAR(preview.preview_angle_degrees, angle * 180.0 / std::acos(-1.0), 0.00001);
	for (const double snap_degrees : {0.0, 90.0, 1.0, 15.0}) {
		const auto repeated = quader_editor_core::preview_rotate_from_screen_position(
			state, {240.0F, 170.0F}, snap_degrees);
		ASSERT_TRUE(repeated.ok);
		expect_transform_near(repeated.transform, preview.transform);
		EXPECT_DOUBLE_EQ(repeated.preview_angle_degrees, preview.preview_angle_degrees);
	}
}

TEST(QdrModelingRuntimeToolRotateTests, TrackballRejectsMissingAndDegenerateCameraFrames)
{
	auto input = trackball_begin_input();
	input.has_camera = false;
	EXPECT_FALSE(quader_editor_core::begin_rotate_drag(input).ok);
	input.has_camera = true;
	input.camera_transform.x_axis = {};
	EXPECT_FALSE(quader_editor_core::begin_rotate_drag(input).ok);
	input.camera_transform.x_axis = { 0.0F, 2.0F, 0.0F };
	EXPECT_FALSE(quader_editor_core::begin_rotate_drag(input).ok);
	input.camera_transform.x_axis = { std::numeric_limits<float>::infinity(), 0.0F, 0.0F };
	EXPECT_FALSE(quader_editor_core::begin_rotate_drag(input).ok);
	input.camera_transform.x_axis = { 1.0F, 0.0F, 0.0F };
	input.camera_transform.y_axis.y = std::numeric_limits<float>::quiet_NaN();
	EXPECT_FALSE(quader_editor_core::begin_rotate_drag(input).ok);
}

TEST(QdrModelingRuntimeToolRotateTests, TrackballRejectsScalarAndInvalidPointerInputWithoutChangingPreview)
{
	const auto begin = quader_editor_core::begin_rotate_drag(trackball_begin_input());
	ASSERT_TRUE(begin.ok);
	auto state = begin.state;
	const auto preview = quader_editor_core::preview_rotate_from_screen_position(
		state, { 250.0F, 200.0F }, 0.0);
	ASSERT_TRUE(preview.ok);
	EXPECT_FALSE(quader_editor_core::preview_rotate_from_angle_degrees(state, 45.0, 0.0).ok);
	expect_transform_near(quader_editor_core::rotate_preview_transform(state), preview.transform);
	EXPECT_FALSE(quader_editor_core::preview_rotate_from_screen_position(
		state, { std::numeric_limits<float>::quiet_NaN(), 150.0F }, 0.0).ok);
	expect_transform_near(quader_editor_core::rotate_preview_transform(state), preview.transform);
	const auto returned = quader_editor_core::preview_rotate_from_screen_position(
		state, { 200.0F, 150.0F }, 0.0);
	ASSERT_TRUE(returned.ok);
	expect_transform_near(returned.transform, {});
}

TEST(QdrModelingRuntimeToolRotateTests, RotatePreviewTransformPreservesManuallyPopulatedScalarStates)
{
	quader_editor_core::RotateToolState state;
	state.axis = quader_editor_core::TransformAxis::X;
	state.preview_angle_degrees = 90.0;
	state.orientation.x_axis = { 0.0F, 1.0F, 0.0F };
	state.orientation.y_axis = { -1.0F, 0.0F, 0.0F };
	const auto transform = quader_editor_core::rotate_preview_transform(state);
	expect_vector_near(transform.z_axis, { 1.0F, 0.0F, 0.0F });
	expect_vector_near(transform.y_axis, { 0.0F, 1.0F, 0.0F });
}

TEST(QdrModelingRuntimeToolRotateTests, RotateDragSnapsScreenAngleAndBuildsPivotedTransform) {
	quader_editor_core::RotateBeginInput input;
        input.axis = quader_editor_core::TransformAxis::Z;
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

TEST(QdrModelingRuntimeToolRotateTests,
     ConstrainedRotationSnapsOnlyCompletedSignedAngularSteps) {
	struct AngleCase {
		double angle;
		double expected;
	};
	const AngleCase cases[] = {
		{14.0, 0.0}, {16.0, 15.0}, {29.0, 15.0}, {31.0, 30.0},
		{15.0, 15.0}, {0.0, 0.0}, {-14.0, 0.0}, {-16.0, -15.0},
		{-29.0, -15.0}, {-31.0, -30.0}, {-15.0, -15.0}, {0.0, 0.0},
	};
	for (const auto axis : {quader_editor_core::TransformAxis::X,
		quader_editor_core::TransformAxis::Y, quader_editor_core::TransformAxis::Z}) {
		quader_editor_core::RotateBeginInput input;
		input.axis = axis;
		input.pivot = {2.0F, 3.0F, 4.0F};
		const auto begin = quader_editor_core::begin_rotate_drag(input);
		ASSERT_TRUE(begin.ok);
		auto state = begin.state;
		for (const AngleCase& angle_case : cases) {
			SCOPED_TRACE(static_cast<int>(axis));
			SCOPED_TRACE(angle_case.angle);
			const auto preview = quader_editor_core::preview_rotate_from_angle_degrees(
				state, angle_case.angle, 15.0);
			ASSERT_TRUE(preview.ok);
			EXPECT_DOUBLE_EQ(preview.preview_angle_degrees, angle_case.expected);
			EXPECT_DOUBLE_EQ(state.accumulated_angle_degrees, angle_case.angle);
			expect_vector_near(
				quader_editor_core::transform_point(preview.pivoted_transform, input.pivot),
				input.pivot);
			const auto unsnapped = quader_editor_core::preview_rotate_from_angle_degrees(
				state, angle_case.angle, 0.0);
			ASSERT_TRUE(unsnapped.ok);
			EXPECT_DOUBLE_EQ(unsnapped.preview_angle_degrees, angle_case.angle);
		}
	}
}

TEST(QdrModelingRuntimeToolRotateTests, RotateDragAccumulatesCircularMotionBeyondHalfTurns) {
	quader_editor_core::RotateBeginInput input;
        input.axis = quader_editor_core::TransformAxis::Z;
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
        input.axis = quader_editor_core::TransformAxis::X;
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

TEST(QdrModelingRuntimeToolRotateTests,
     RotateDragConjugatesTheAxisDeltaThroughItsOrientation) {
	quader_editor_core::RotateBeginInput input;
	input.axis = quader_editor_core::TransformAxis::X;
	input.orientation.x_axis = {0.0F, 1.0F, 0.0F};
	input.orientation.y_axis = {-1.0F, 0.0F, 0.0F};
	input.pivot = {};
	input.screen_position = {120.0F, 100.0F};
	input.has_screen_pivot = true;
	input.screen_pivot = {100.0F, 100.0F};

	const quader_editor_core::RotateBeginResult begin =
		quader_editor_core::begin_rotate_drag(input);
	ASSERT_TRUE(begin.ok);
	quader_editor_core::RotateToolState state = begin.state;
	const quader_editor_core::RotatePreviewResult preview =
		quader_editor_core::preview_rotate_from_angle_degrees(state, 90.0, 0.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.transform.x_axis.x, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.x_axis.y, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.x_axis.z, -1.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.y_axis.x, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.y_axis.y, 1.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.y_axis.z, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.z_axis.x, 1.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.z_axis.y, 0.0F, 0.0001F);
	EXPECT_NEAR(preview.transform.z_axis.z, 0.0F, 0.0001F);
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
                quader_editor_core::ModalTransformDragTool::Rotate,
                true,
                0.0,
                {1.0F, 1.0F, 1.0F},
                quader_editor_core::kToolEpsilon,
            });
        EXPECT_TRUE(update.handled);
	EXPECT_TRUE(update.preview_ok);
	EXPECT_FALSE(update.preview_valid);
        EXPECT_TRUE(
            update.route ==
            quader_editor_core::ModalTransformPreviewRoute::ClearPreview);

        const quader_editor_core::ModalTransformCommitPlan commit =
            quader_editor_core::plan_modal_transform_drag_commit({
                true,
                quader_editor_core::ModalTransformDragTool::Rotate,
                false,
                true,
            });
        EXPECT_TRUE(commit.handled);
	EXPECT_TRUE(commit.restore_base_snapshot);
	EXPECT_TRUE(commit.restore_selection);
	EXPECT_FALSE(commit.model_changed);
}

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
     MoveAllDragUsesTwoViewPlaneComponentsWithoutAnAxisConstraint) {
	constexpr float kInvSqrtTen = 0.31622777F;
	constexpr float kInvSqrtFourteen = 0.26726124F;
	constexpr float kInvSqrtThirtyFive = 0.16903085F;
	const quader::QVec3 view_right {
		3.0F * kInvSqrtTen, 0.0F, -kInvSqrtTen};
	const quader::QVec3 view_up {
		-kInvSqrtThirtyFive, 5.0F * kInvSqrtThirtyFive,
		-3.0F * kInvSqrtThirtyFive};
	const quader::QVec3 view_normal {
		kInvSqrtFourteen, 2.0F * kInvSqrtFourteen,
		3.0F * kInvSqrtFourteen};
	const quader::QVec3 center {2.0F, -1.0F, 0.5F};
	const quader::QVec3 expected_delta {
		view_right.x * 1.25F + view_up.x * -0.75F,
		view_right.y * 1.25F + view_up.y * -0.75F,
		view_right.z * 1.25F + view_up.z * -0.75F,
	};

	quader_editor_core::MoveBeginInput input;
	input.axis = quader_editor_core::TransformAxis::All;
	input.center_local = center;
	input.has_world_center = true;
	input.world_center = center;
	input.screen_position = {100.0F, 100.0F};
	input.has_start_ray = true;
	input.start_ray = ray_through_plane_point(center, view_normal);
	input.has_camera = true;
	input.camera_transform.origin = {
		center.x + view_normal.x * 6.0F,
		center.y + view_normal.y * 6.0F,
		center.z + view_normal.z * 6.0F,
	};
	input.camera_transform.x_axis = view_right;
	input.camera_transform.y_axis = view_up;
	input.camera_transform.z_axis = view_normal;

	const quader_editor_core::MoveBeginResult begin =
		quader_editor_core::begin_move_drag(input);
	ASSERT_TRUE(begin.ok);
	EXPECT_TRUE(begin.state.axis == quader_editor_core::TransformAxis::All);
	EXPECT_NEAR(begin.state.world_plane_normal.x, view_normal.x, 0.0001F);
	EXPECT_NEAR(begin.state.world_plane_normal.y, view_normal.y, 0.0001F);
	EXPECT_NEAR(begin.state.world_plane_normal.z, view_normal.z, 0.0001F);

	quader_editor_core::MoveToolState state = begin.state;
	const quader::QVec3 target {
		center.x + expected_delta.x,
		center.y + expected_delta.y,
		center.z + expected_delta.z,
	};
	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_from_viewport(
			state, {140.0F, 124.0F},
			ray_through_plane_point(target, view_normal),
			front_camera(), false, false, 1.0);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(preview.preview_delta.x, expected_delta.x, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.y, expected_delta.y, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.z, expected_delta.z, 0.0001F);
	const float right_component =
		preview.preview_delta.x * view_right.x +
		preview.preview_delta.y * view_right.y +
		preview.preview_delta.z * view_right.z;
	const float up_component =
		preview.preview_delta.x * view_up.x +
		preview.preview_delta.y * view_up.y +
		preview.preview_delta.z * view_up.z;
	const float normal_component =
		preview.preview_delta.x * view_normal.x +
		preview.preview_delta.y * view_normal.y +
		preview.preview_delta.z * view_normal.z;
	EXPECT_NEAR(right_component, 1.25F, 0.0001F);
	EXPECT_NEAR(up_component, -0.75F, 0.0001F);
	EXPECT_NEAR(normal_component, 0.0F, 0.0001F);

	for (const bool snap_center_to_grid : {false, true}) {
		SCOPED_TRACE(snap_center_to_grid ? "snap center" : "snap delta");
		quader_editor_core::MoveToolState snapped_state = begin.state;
		const quader_editor_core::MovePreviewResult snapped_preview =
			quader_editor_core::preview_move_from_viewport(
				snapped_state, {140.0F, 124.0F},
				ray_through_plane_point(target, view_normal),
				front_camera(), false, true, 1.0,
				snap_center_to_grid);
		ASSERT_TRUE(snapped_preview.ok);
		EXPECT_GT(quader::length(snapped_preview.preview_delta), 0.01F);
		EXPECT_NEAR(
			snapped_preview.preview_delta.x * view_normal.x +
				snapped_preview.preview_delta.y * view_normal.y +
				snapped_preview.preview_delta.z * view_normal.z,
			0.0F, 0.0001F);
	}
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
	struct MidpointCase {
		float motion;
		float expected_delta;
	};
	const MidpointCase cases[] = {
		{0.55F, 0.8F}, {0.5499F, 0.3F}, {0.5501F, 0.8F},
		{-0.95F, -0.7F}, {-0.9501F, -1.2F}, {-0.9499F, -0.7F},
	};
	for (const MidpointCase& midpoint_case : cases) {
		SCOPED_TRACE(midpoint_case.motion);
		quader_editor_core::MoveToolState state = begin_plane_move(
			quader_editor_core::TransformAxis::Xy, orientation, center);
		const quader::QVec3 raw_delta =
			transformed_vector(orientation, {midpoint_case.motion, 0.9F, 0.0F});
		const quader::QVec3 target{
			center.x + raw_delta.x,
			center.y + raw_delta.y,
			center.z + raw_delta.z,
		};
		const quader::QVec3 expected =
			transformed_vector(orientation, {midpoint_case.expected_delta, 0.8F, 0.0F});

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

TEST(QdrModelingRuntimeToolMoveTests,
     AnchorGridCatchupUsesSignedPositiveHalfTiesAndFrozenStart) {
	struct MoveCase {
		float anchor;
		float motion;
		float expected;
	};
	const MoveCase cases[] = {
		{6.0F, 1.0F, 2.0F}, {6.0F, 7.0F, 10.0F},
		{6.0F, -3.0F, -6.0F}, {-6.0F, -1.0F, -2.0F},
		{-6.0F, -7.0F, -10.0F}, {-6.0F, 2.0F, 6.0F},
		{-10.0F, -2.0F, 2.0F}, {0.0F, -4.0F, 0.0F},
		{0.0F, 4.0F, 8.0F}, {6.0F, 0.0F, 0.0F},
		{16777216.0F, 3.0F, 0.0F}, {-16777216.0F, -5.0F, -8.0F},
	};
	auto camera = front_camera();
	camera.projection = quader_editor_core::CameraProjection::Orthographic;
	camera.orthographic_size = camera.viewport_size.y;
	for (const MoveCase& move_case : cases) {
		SCOPED_TRACE(move_case.anchor);
		SCOPED_TRACE(move_case.motion);
		auto state = begin_axis_move(
			quader_editor_core::TransformAxis::X, {move_case.anchor, 3.0F, 5.0F});
		for (const bool grid_enabled : {true, false, true}) {
			const auto preview = quader_editor_core::preview_move_from_viewport(
				state, {100.0F + move_case.motion, 100.0F}, {}, camera,
				false, grid_enabled, 8.0, true);
			ASSERT_TRUE(preview.ok);
			EXPECT_NEAR(preview.preview_delta.x,
				grid_enabled ? move_case.expected : move_case.motion, 0.0001F);
			EXPECT_FLOAT_EQ(preview.preview_delta.y, 0.0F);
			EXPECT_FLOAT_EQ(preview.preview_delta.z, 0.0F);
		}
		const auto returned = quader_editor_core::preview_move_from_viewport(
			state, {100.0F, 100.0F}, {}, camera, false, true, 8.0, true);
		ASSERT_TRUE(returned.ok);
		EXPECT_FLOAT_EQ(returned.preview_delta.x, 0.0F);
	}
}

TEST(QdrModelingRuntimeToolMoveTests,
     OrdinaryComponentAnchorCatchupDoesNotDependOnScenePreviewEligibility) {
	auto camera = front_camera();
	camera.projection = quader_editor_core::CameraProjection::Orthographic;
	camera.orthographic_size = camera.viewport_size.y;
	for (const auto mode : {
		quader_editor_core::SelectionMode::Object,
		quader_editor_core::SelectionMode::Vertex,
		quader_editor_core::SelectionMode::Edge,
		quader_editor_core::SelectionMode::Face}) {
		for (const bool scene_preview : {false, true}) {
			SCOPED_TRACE(static_cast<int>(mode));
			SCOPED_TRACE(scene_preview);
			quader_editor_core::TransformDragState drag;
			drag.active = true;
			drag.tool = quader_editor_core::ViewportInteractionTool::Move;
			drag.axis = quader_editor_core::TransformAxis::X;
			drag.scene_transform_preview_eligible = scene_preview;
			drag.move = begin_axis_move(drag.axis, {6.0F, 0.0F, 0.0F});
			quader_editor_core::TransformDragPreviewInput input;
			input.camera = camera;
			input.grid_enabled = true;
			input.grid_size = 8.0;
			input.screen_position = {101.0F, 100.0F};
			const auto preview = quader_editor_core::apply_transform_drag_preview(
				{}, mode, drag, input);
			ASSERT_TRUE(preview.ok);
			EXPECT_NEAR(drag.move.preview_delta.x, 2.0F, 0.0001F);
			EXPECT_EQ(drag.scene_transform_preview_eligible, scene_preview);
		}
	}
}

TEST(QdrModelingRuntimeToolMoveTests,
     LocalPlaneAnchorCatchupPreservesExcludedCoordinatesAndZeroMotion) {
	const quader_poly::Transform3 orientation{
		{1.0F, 0.0F, 0.0F}, {0.5F, 1.0F, 0.0F}, {0.5F, 0.0F, 1.0F}, {}};
	const quader::QVec3 center = transformed_vector(orientation, {6.0F, -6.0F, 10.0F});
	struct PlaneCase {
		quader_editor_core::TransformAxis axis;
		quader::QVec3 motion;
		quader::QVec3 expected;
	};
	const PlaneCase cases[] = {
		{quader_editor_core::TransformAxis::Xy, {1.0F, 2.0F, 0.0F}, {2.0F, 6.0F, 0.0F}},
		{quader_editor_core::TransformAxis::Xz, {1.0F, 0.0F, -6.0F}, {2.0F, 0.0F, -2.0F}},
		{quader_editor_core::TransformAxis::Yz, {0.0F, 2.0F, -6.0F}, {0.0F, 6.0F, -2.0F}},
	};
	for (const PlaneCase& plane_case : cases) {
		SCOPED_TRACE(static_cast<int>(plane_case.axis));
		auto state = begin_plane_move(plane_case.axis, orientation, center);
		const auto normal = included_plane_normal(orientation, plane_case.axis);
		const auto motion = transformed_vector(orientation, plane_case.motion);
		const auto expected = transformed_vector(orientation, plane_case.expected);
		for (const bool grid_enabled : {true, false}) {
			const auto preview = quader_editor_core::preview_move_from_viewport(
				state, {101.0F, 102.0F}, ray_through_plane_point(center + motion, normal),
				front_camera(), false, grid_enabled, 8.0, true);
			ASSERT_TRUE(preview.ok);
			const auto expected_delta = grid_enabled ? expected : motion;
			EXPECT_NEAR(preview.preview_delta.x, expected_delta.x, 0.0001F);
			EXPECT_NEAR(preview.preview_delta.y, expected_delta.y, 0.0001F);
			EXPECT_NEAR(preview.preview_delta.z, expected_delta.z, 0.0001F);
		}
		const auto returned = quader_editor_core::preview_move_from_viewport(
			state, {100.0F, 100.0F}, ray_through_plane_point(center, normal),
			front_camera(), false, true, 8.0, true);
		ASSERT_TRUE(returned.ok);
		EXPECT_NEAR(quader::length(returned.preview_delta), 0.0F, 0.0001F);
	}
}

TEST(QdrModelingRuntimeToolMoveTests,
     AxisMoveDragProjectsHoveredVertexTargetOntoTheActiveAxis) {
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

TEST(QdrModelingRuntimeToolMoveTests,
     FreeMoveDragSnapsTheCenterExactlyToTheHoveredVertexTarget) {
	const quader_poly::Transform3 model_transform =
		nonorthogonal_orientation();
	const quader::QVec3 center {2.0F, -3.0F, 4.0F};
	const quader::QVec3 expected_local_delta {1.25F, -0.5F, 2.0F};
	const quader::QVec3 expected_world_delta =
		transformed_vector(model_transform, expected_local_delta);

	quader_editor_core::MoveToolState state;
	state.active = true;
	state.axis = quader_editor_core::TransformAxis::All;
	state.model_transform = model_transform;
	state.center_world = center;

	ASSERT_TRUE(quader_editor_core::move_axis_supports_vertex_snap(
		quader_editor_core::TransformAxis::All));
	const quader_editor_core::MovePreviewResult preview =
		quader_editor_core::preview_move_to_axis_snap_target(
			state, center + expected_world_delta);

	ASSERT_TRUE(preview.ok);
	EXPECT_NEAR(
		preview.preview_distance, quader::length(expected_world_delta),
		0.0001);
	EXPECT_NEAR(preview.preview_delta.x, expected_local_delta.x, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.y, expected_local_delta.y, 0.0001F);
	EXPECT_NEAR(preview.preview_delta.z, expected_local_delta.z, 0.0001F);
	const quader::QVec3 resolved_world_target =
		center + transformed_vector(model_transform, preview.preview_delta);
	EXPECT_NEAR(
		resolved_world_target.x, center.x + expected_world_delta.x, 0.0001F);
	EXPECT_NEAR(
		resolved_world_target.y, center.y + expected_world_delta.y, 0.0001F);
	EXPECT_NEAR(
		resolved_world_target.z, center.z + expected_world_delta.z, 0.0001F);
}

TEST(QdrModelingRuntimeToolMoveTests,
     FreeMoveSnapsTheSelectedVertexOntoAnotherVertexDespiteGridQuantization) {
	quader_editor_core::PolygonObject object = cube_object(1);
	const quader_poly::Vertex source_vertex =
		object.document.read().vertices.at(0);
	const quader_poly::Vertex target_vertex =
		object.document.read().vertices.at(2);
	quader_editor_core::EditorSession session({object});
	quader_poly::Selection selection;
	selection.vertices = {source_vertex.id};
	session.set_component_selection(
		object.id, selection,
		quader_editor_core::SelectionMode::Vertex);

	quader_editor_core::TransformDragState drag;
	drag.active = true;
	drag.tool = quader_editor_core::ViewportInteractionTool::Move;
	drag.operation = quader_editor_core::TransformDragOperation::Transform;
	drag.axis = quader_editor_core::TransformAxis::All;
	drag.scene_transform_preview_eligible = true;
	drag.move.active = true;
	drag.move.axis = quader_editor_core::TransformAxis::All;
	drag.move.center_world = source_vertex.position;

	quader_editor_core::TransformDragPreviewInput input;
	input.camera = front_camera();
	input.screen_position = {100.0F, 100.0F};
	input.ray = {
		{target_vertex.position.x, target_vertex.position.y, 6.0F},
		{0.0F, 0.0F, -1.0F}};
	input.grid_enabled = true;
	input.grid_size = 100.0;
	input.vertex_snap_enabled = true;
	input.vertex_snap_radius = 0.2;

	const quader_editor_core::CommandResult result =
		quader_editor_core::apply_transform_drag_preview(session, drag, input);

	ASSERT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(
		drag.snap_target.kind ==
		quader_editor_core::TransformSnapLandmarkKind::Vertex);
	EXPECT_TRUE(drag.snap_target.object_id == object.id);
	EXPECT_TRUE(drag.snap_target.vertex_id == target_vertex.id);
	EXPECT_NEAR(
		source_vertex.position.x + drag.move.preview_delta.x,
		target_vertex.position.x, 0.0001F);
	EXPECT_NEAR(
		source_vertex.position.y + drag.move.preview_delta.y,
		target_vertex.position.y, 0.0001F);
	EXPECT_NEAR(
		source_vertex.position.z + drag.move.preview_delta.z,
		target_vertex.position.z, 0.0001F);
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

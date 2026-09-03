////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

#include <cmath>

namespace {

void expect_vector_near(quader::QVec3 actual, quader::QVec3 expected)
{
	EXPECT_NEAR(actual.x, expected.x, 0.0001F);
	EXPECT_NEAR(actual.y, expected.y, 0.0001F);
	EXPECT_NEAR(actual.z, expected.z, 0.0001F);
}

double vector_length(quader::QVec3 value)
{
	return std::sqrt(
		static_cast<double>(value.x) * value.x +
		static_cast<double>(value.y) * value.y +
		static_cast<double>(value.z) * value.z);
}

quader::QVec3 rotate_around_y(quader::QVec3 value, double angle)
{
	const double cosine = std::cos(angle);
	const double sine = std::sin(angle);
	return {
		static_cast<float>(cosine * value.x + sine * value.z),
		value.y,
		static_cast<float>(cosine * value.z - sine * value.x),
	};
}

} // namespace

TEST(QdrModelingRuntimeToolViewportNavigationTests, ViewportNavigationStateTracksOrbitPanAndFlyOwnership) {
	quader_editor_core::ViewportNavigationState state;
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, false);
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_orbit(state));
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_pan(state));
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_active(state));

	quader_editor_core::begin_viewport_navigation(state, true);
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_pan(state));
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_orbit(state));

	quader_editor_core::begin_viewport_fly_navigation(state, 77, "main");
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_fly(state));
	EXPECT_TRUE(quader_editor_core::viewport_navigation_is_fly_camera(state, 77));
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_fly_camera(state, 78));

	quader_editor_core::end_viewport_navigation(state);
	EXPECT_FALSE(quader_editor_core::viewport_navigation_is_active(state));
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	PerspectiveZoomHonorsSoftBoundaryBeforeMovingTheCamera)
{
	quader_editor_core::ViewportZoomInput input;
	input.camera.transform.origin = { 0.0F, 0.0F, 500.0F };
	input.pivot = {};
	input.steps = -1.0;

	const quader_editor_core::ViewportNavigationResult saturated =
		quader_editor_core::viewport_zoom(input);

	ASSERT_TRUE(saturated.ok);
	expect_vector_near(saturated.camera_transform.origin,
		input.camera.transform.origin);
	expect_vector_near(saturated.pivot, input.pivot);

	input.steps = 1.0;
	const quader_editor_core::ViewportNavigationResult inward =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(inward.ok);
	EXPECT_NEAR(inward.camera_transform.origin.z, 500.0 / 1.18, 0.0001);
	expect_vector_near(inward.pivot, input.pivot);

	input.camera.transform.origin = { 0.0F, 0.0F, 1000.0F };
	input.steps = -1.0;
	const quader_editor_core::ViewportNavigationResult invalid_outward =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(invalid_outward.ok);
	expect_vector_near(invalid_outward.camera_transform.origin,
		input.camera.transform.origin);

	input.steps = 1.0;
	const quader_editor_core::ViewportNavigationResult gradual_inward =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(gradual_inward.ok);
	EXPECT_NEAR(gradual_inward.camera_transform.origin.z,
		1000.0 / 1.18, 0.0001);

	input.camera.transform.origin = { 0.0F, 0.0F, 550.0F };
	const quader_editor_core::ViewportNavigationResult crosses_cap_inward =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(crosses_cap_inward.ok);
	EXPECT_NEAR(crosses_cap_inward.camera_transform.origin.z,
		550.0 / 1.18, 0.0001);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	PositiveSubMinimumPerspectiveDistanceZoomsOutMultiplicatively)
{
	quader_editor_core::ViewportZoomInput input;
	input.camera.transform.origin = { 0.0F, 0.0F, 0.01F };
	input.pivot = {};
	input.steps = -1.0;

	const quader_editor_core::ViewportNavigationResult result =
		quader_editor_core::viewport_zoom(input);

	ASSERT_TRUE(result.ok);
	EXPECT_NEAR(result.camera_transform.origin.z, 0.01 * 1.18, 0.000001);
	expect_vector_near(result.pivot, input.pivot);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	ExceptionalPerspectiveZoomEnvelopeRoundTripsInBothDirections)
{
	quader_editor_core::ViewportZoomInput input;
	input.camera.transform.origin = { 0.0F, 0.0F, 900.0F };
	input.pivot = {};
	input.limits.maximum_perspective_distance = 900.0;
	input.steps = 1.0;

	const quader_editor_core::ViewportNavigationResult zoomed_in =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(zoomed_in.ok);
	EXPECT_NEAR(zoomed_in.camera_transform.origin.z, 900.0 / 1.18,
		0.0001);

	input.camera.transform = zoomed_in.camera_transform;
	input.steps = -1.0;
	const quader_editor_core::ViewportNavigationResult restored_far =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(restored_far.ok);
	EXPECT_NEAR(restored_far.camera_transform.origin.z, 900.0, 0.0001);

	input.camera.transform.origin = { 0.0F, 0.0F, 0.01F };
	input.limits.minimum_perspective_distance = 0.01;
	input.limits.maximum_perspective_distance =
		quader_editor_core::kNavigationMaxCameraDistance;
	input.steps = -1.0;
	const quader_editor_core::ViewportNavigationResult zoomed_out =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(zoomed_out.ok);
	EXPECT_NEAR(zoomed_out.camera_transform.origin.z, 0.01 * 1.18,
		0.000001);

	input.camera.transform = zoomed_out.camera_transform;
	input.steps = 1.0;
	const quader_editor_core::ViewportNavigationResult restored_near =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(restored_near.ok);
	EXPECT_NEAR(restored_near.camera_transform.origin.z, 0.01, 0.000001);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	PerspectiveSoftLimitDoesNotTruncateAnAcceptedDetent)
{
	quader_editor_core::ViewportZoomInput input;
	input.camera.transform.origin = { 0.0F, 0.0F, 450.0F };
	input.pivot = {};
	input.steps = -1.0;

	const quader_editor_core::ViewportNavigationResult crossed =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(crossed.ok);
	EXPECT_NEAR(crossed.camera_transform.origin.z, 450.0 * 1.18,
		0.0001);

	input.camera.transform = crossed.camera_transform;
	input.steps = 1.0;
	const quader_editor_core::ViewportNavigationResult restored =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(restored.ok);
	EXPECT_NEAR(restored.camera_transform.origin.z, 450.0, 0.0001);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	ExceptionalOrthographicZoomEnvelopeRoundTrips)
{
	quader_editor_core::ViewportZoomInput input;
	input.camera.projection = quader_editor_core::CameraProjection::Orthographic;
	input.camera.orthographic_size = 2000.0;
	input.limits.maximum_orthographic_size = 2000.0;
	input.steps = 1.0;

	const quader_editor_core::ViewportNavigationResult zoomed_in =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(zoomed_in.ok);
	ASSERT_TRUE(zoomed_in.has_camera_size);
	EXPECT_NEAR(zoomed_in.camera_size, 2000.0 / 1.18, 0.0001);

	input.camera.orthographic_size = zoomed_in.camera_size;
	input.steps = -1.0;
	const quader_editor_core::ViewportNavigationResult restored =
		quader_editor_core::viewport_zoom(input);
	ASSERT_TRUE(restored.ok);
	ASSERT_TRUE(restored.has_camera_size);
	EXPECT_NEAR(restored.camera_size, 2000.0, 0.0001);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	OrthographicCursorZoomReversesAfterCrossingTheSoftMinimum)
{
	constexpr double kInitialSize = 0.011;
	const quader::QVec3 initial_origin{ 0.0F, 0.0F, 10.0F };
	const quader::QVec3 cursor_target{ 2.0F, 1.0F, 0.0F };
	quader_editor_core::ViewportZoomToCursorInput input;
	input.camera.projection = quader_editor_core::CameraProjection::Orthographic;
	input.camera.transform.origin = initial_origin;
	input.camera.orthographic_size = kInitialSize;
	input.pivot = cursor_target;
	input.ray_origin = { 2.0F, 1.0F, 10.0F };
	input.ray_direction = { 0.0F, 0.0F, -1.0F };
	input.cursor_target = cursor_target;
	input.has_cursor_target = true;
	input.steps = 1.0;

	const quader_editor_core::ViewportNavigationResult zoomed_in =
		quader_editor_core::viewport_zoom_to_cursor(input);
	ASSERT_TRUE(zoomed_in.ok);
	ASSERT_TRUE(zoomed_in.has_camera_size);
	ASSERT_LT(zoomed_in.camera_size,
		quader_editor_core::kNavigationMinOrthographicSize);
	EXPECT_NEAR(zoomed_in.camera_size,
		kInitialSize / quader_editor_core::kNavigationZoomStepFactor,
		0.000001);
	EXPECT_GT(vector_length(
		zoomed_in.camera_transform.origin - initial_origin), 0.0);
	expect_vector_near(zoomed_in.pivot, cursor_target);

	input.camera.transform = zoomed_in.camera_transform;
	input.camera.orthographic_size = zoomed_in.camera_size;
	input.steps = -1.0;
	const quader_editor_core::ViewportNavigationResult restored =
		quader_editor_core::viewport_zoom_to_cursor(input);

	ASSERT_TRUE(restored.ok);
	ASSERT_TRUE(restored.has_camera_size);
	EXPECT_NEAR(restored.camera_size, kInitialSize, 0.000001);
	expect_vector_near(restored.camera_transform.origin, initial_origin);
	expect_vector_near(restored.pivot, cursor_target);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	OrthographicCursorZoomAtMinimumDoesNotDriftOnRepeatedPackets)
{
	const double minimum_size =
		quader_editor_core::kNavigationMinOrthographicSize /
		quader_editor_core::kNavigationZoomStepFactor;
	quader_editor_core::ViewportZoomToCursorInput input;
	input.camera.projection = quader_editor_core::CameraProjection::Orthographic;
	input.camera.transform.origin = { 0.0F, 0.0F, 10.0F };
	input.camera.orthographic_size = minimum_size;
	input.pivot = { 4.0F, -3.0F, 0.0F };
	input.ray_origin = { 2.0F, 1.0F, 10.0F };
	input.ray_direction = { 0.0F, 0.0F, -1.0F };
	input.limits.minimum_orthographic_size = minimum_size;
	input.steps = 1.0;
	const quader_poly::Transform3 initial_transform = input.camera.transform;
	const quader::QVec3 initial_pivot = input.pivot;

	for (int packet = 0; packet < 8; ++packet) {
		const quader_editor_core::ViewportNavigationResult result =
			quader_editor_core::viewport_zoom_to_cursor(input);
		ASSERT_TRUE(result.ok);
		ASSERT_TRUE(result.has_camera_size);
		EXPECT_DOUBLE_EQ(result.camera_size, minimum_size);
		expect_vector_near(result.camera_transform.origin,
			initial_transform.origin);
		expect_vector_near(result.camera_transform.x_axis,
			initial_transform.x_axis);
		expect_vector_near(result.camera_transform.y_axis,
			initial_transform.y_axis);
		expect_vector_near(result.camera_transform.z_axis,
			initial_transform.z_axis);
		expect_vector_near(result.pivot, initial_pivot);
		input.camera.transform = result.camera_transform;
		input.camera.orthographic_size = result.camera_size;
		input.pivot = result.pivot;
	}
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	CursorZoomScalesCameraAndPivotReversiblyAroundTheDepthPlane)
{
	quader_editor_core::ViewportZoomToCursorInput input;
	input.camera.transform.origin = { 0.0F, 0.0F, 100.0F };
	input.camera.viewport_size = { 800.0F, 600.0F };
	input.pivot = {};
	input.ray_origin = input.camera.transform.origin;
	input.ray_direction = { 0.4472136F, 0.0F, -0.8944272F };
	input.steps = 2.5;

	const quader_editor_core::ViewportNavigationResult zoomed =
		quader_editor_core::viewport_zoom_to_cursor(input);
	ASSERT_TRUE(zoomed.ok);

	input.camera.transform = zoomed.camera_transform;
	input.pivot = zoomed.pivot;
	input.ray_origin = zoomed.camera_transform.origin;
	input.steps = -2.5;
	const quader_editor_core::ViewportNavigationResult restored =
		quader_editor_core::viewport_zoom_to_cursor(input);

	ASSERT_TRUE(restored.ok);
	expect_vector_near(restored.camera_transform.origin,
		{ 0.0F, 0.0F, 100.0F });
	expect_vector_near(restored.pivot, {});
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	OrbitAroundDynamicAnchorRigidlyRotatesEyeAndVisiblePivot)
{
	quader_editor_core::ViewportOrbitInput input;
	input.camera_transform.origin = { 0.0F, 0.0F, 10.0F };
	input.pivot = {};
	input.screen_delta = { 100.0F, 0.0F };
	input.sensitivity = 0.01;
	input.orbit_anchor = { 2.0F, 1.0F, 0.0F };
	input.has_orbit_anchor = true;

	quader_editor_core::ViewportCameraState before_camera;
	before_camera.transform = input.camera_transform;
	before_camera.viewport_size = { 800.0F, 600.0F };
	quader::QVec2 before_anchor_screen;
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		before_camera, input.orbit_anchor, before_anchor_screen));

	const quader_editor_core::ViewportNavigationResult result =
		quader_editor_core::viewport_orbit(input);

	ASSERT_TRUE(result.ok);
	const double yaw = -static_cast<double>(input.screen_delta.x) *
		input.sensitivity;
	expect_vector_near(
		result.camera_transform.origin,
		input.orbit_anchor + rotate_around_y(
			input.camera_transform.origin - input.orbit_anchor, yaw));
	expect_vector_near(
		result.pivot,
		input.orbit_anchor +
			rotate_around_y(input.pivot - input.orbit_anchor, yaw));
	EXPECT_NEAR(vector_length(result.camera_transform.origin - result.pivot),
		10.0, 0.0001);

	quader_editor_core::ViewportCameraState after_camera = before_camera;
	after_camera.transform = result.camera_transform;
	quader::QVec2 after_anchor_screen;
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		after_camera, input.orbit_anchor, after_anchor_screen));
	EXPECT_NEAR(after_anchor_screen.x, before_anchor_screen.x, 0.001F);
	EXPECT_NEAR(after_anchor_screen.y, before_anchor_screen.y, 0.001F);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	OrbitWithoutDynamicAnchorKeepsTheVisiblePivotFixed)
{
	quader_editor_core::ViewportOrbitInput input;
	input.camera_transform.origin = { 0.0F, 0.0F, 10.0F };
	input.pivot = { 0.0F, 0.0F, 0.0F };
	input.screen_delta = { 75.0F, -20.0F };
	input.sensitivity = 0.01;

	const quader_editor_core::ViewportNavigationResult result =
		quader_editor_core::viewport_orbit(input);

	ASSERT_TRUE(result.ok);
	expect_vector_near(result.pivot, input.pivot);
	EXPECT_NEAR(vector_length(result.camera_transform.origin - result.pivot),
		10.0, 0.0001);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	OrbitSnapRotatesTheVisiblePivotAroundTheDynamicAnchor)
{
	constexpr double kNearAxisAngle = 0.05;
	const quader::QVec3 anchor{ 2.0F, 1.0F, 0.0F };
	const quader::QVec3 base_eye{ 0.0F, 0.0F, 10.0F };
	const quader::QVec3 base_pivot{};

	quader_editor_core::ViewportOrbitInput input;
	input.camera_transform.x_axis =
		rotate_around_y({ 1.0F, 0.0F, 0.0F }, kNearAxisAngle);
	input.camera_transform.y_axis = { 0.0F, 1.0F, 0.0F };
	input.camera_transform.z_axis =
		rotate_around_y({ 0.0F, 0.0F, 1.0F }, kNearAxisAngle);
	input.camera_transform.origin =
		anchor + rotate_around_y(base_eye - anchor, kNearAxisAngle);
	input.pivot =
		anchor + rotate_around_y(base_pivot - anchor, kNearAxisAngle);
	input.orbit_anchor = anchor;
	input.has_orbit_anchor = true;
	input.snap_to_orthogonal = true;

	quader_editor_core::ViewportCameraState before_camera;
	before_camera.transform = input.camera_transform;
	before_camera.viewport_size = { 800.0F, 600.0F };
	quader::QVec2 before_anchor_screen;
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		before_camera, anchor, before_anchor_screen));

	const quader_editor_core::ViewportNavigationResult result =
		quader_editor_core::viewport_orbit(input);

	ASSERT_TRUE(result.ok);
	EXPECT_TRUE(result.orthogonal_snap);
	expect_vector_near(result.camera_transform.origin, base_eye);
	expect_vector_near(result.pivot, base_pivot);

	quader_editor_core::ViewportCameraState after_camera = before_camera;
	after_camera.transform = result.camera_transform;
	quader::QVec2 after_anchor_screen;
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		after_camera, anchor, after_anchor_screen));
	EXPECT_NEAR(after_anchor_screen.x, before_anchor_screen.x, 0.001F);
	EXPECT_NEAR(after_anchor_screen.y, before_anchor_screen.y, 0.001F);
}

TEST(QdrModelingRuntimeToolViewportNavigationTests,
	HugeOrthographicFrameBoundsKeepRequiredEyeDistanceAndCenterCanonical)
{
	quader_editor_core::ViewportFrameBoundsInput input;
	input.camera.projection = quader_editor_core::CameraProjection::Orthographic;
	input.camera.viewport_size = { 1920.0F, 1080.0F };
	input.min_bounds = { -10000.0F, -20000.0F, -30000.0F };
	input.max_bounds = { 30000.0F, 40000.0F, 50000.0F };
	const quader::QVec3 expected_center{ 10000.0F, 10000.0F, 10000.0F };

	const quader_editor_core::ViewportNavigationResult result =
		quader_editor_core::viewport_frame_bounds(input);

	ASSERT_TRUE(result.ok);
	ASSERT_TRUE(result.has_camera_size);
	expect_vector_near(result.pivot, expected_center);
	EXPECT_GT(vector_length(result.camera_transform.origin - result.pivot),
		quader_editor_core::kNavigationMaxCameraDistance);
	EXPECT_DOUBLE_EQ(result.camera_size,
		quader_editor_core::kNavigationMaxOrthographicSize);
}

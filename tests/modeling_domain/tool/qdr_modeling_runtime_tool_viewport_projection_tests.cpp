////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

#include <cmath>
#include <limits>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
        camera.transform.origin = { 0.0F, 0.0F, 5.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

quader_editor_core::ViewportCameraState affine_front_camera()
{
	quader_editor_core::ViewportCameraState camera = front_camera();
	camera.affine_model_space = true;
	// Object-to-world is scale(2, 0.5, 1.5) followed by translation
	// (3, -4, 2). Camera axes are the M^T world-camera covectors and the
	// origin is M^-1 times the world-camera origin.
	camera.transform.origin = { -1.5F, 8.0F, 2.0F };
	camera.transform.x_axis = { 2.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 0.5F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.5F };
	return camera;
}

quader::QVec3 world_from_test_object(quader::QVec3 local)
{
	return {
		3.0F + local.x * 2.0F,
		-4.0F + local.y * 0.5F,
		2.0F + local.z * 1.5F,
	};
}

} // namespace

TEST(QdrModelingRuntimeToolViewportProjectionTests, ViewportProjectionAndRayConstructionRoundTripScreenCenter) {
	const quader_editor_core::ViewportCameraState camera = front_camera();
	quader::QVec2 screen;
	EXPECT_TRUE(quader_editor_core::project_viewport_point(camera, { 0.0F, 0.0F, 0.0F }, screen));
	EXPECT_NEAR(screen.x, 400.0F, 0.001F);
	EXPECT_NEAR(screen.y, 300.0F, 0.001F);
	EXPECT_FALSE(quader_editor_core::viewport_point_is_behind_camera(camera, { 0.0F, 0.0F, 0.0F }));
	EXPECT_TRUE(quader_editor_core::viewport_point_is_behind_camera(camera, { 0.0F, 0.0F, 7.0F }));

	const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(camera, { 400.0F, 300.0F });
	EXPECT_NEAR(ray.origin.z, 5.0F, 0.0001);
	EXPECT_NEAR(ray.direction.x, 0.0F, 0.0001F);
	EXPECT_NEAR(ray.direction.y, 0.0F, 0.0001F);
	EXPECT_NEAR(ray.direction.z, -1.0F, 0.0001F);
}

TEST(QdrModelingRuntimeToolViewportProjectionTests, PerspectiveViewportProjectionPreservesOffCenterRayRoundTrip) {
	const quader_editor_core::ViewportCameraState camera = front_camera();
	const quader::QVec3 point { 2.0F, 1.0F, 0.0F };
	quader::QVec2 screen;
	EXPECT_TRUE(quader_editor_core::project_viewport_point(camera, point, screen));
	EXPECT_TRUE(screen.x > 400.0F);
	EXPECT_TRUE(screen.y < 300.0F);

	const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(camera, screen);
	const double expected_length = std::sqrt(2.0 * 2.0 + 1.0 * 1.0 + 5.0 * 5.0);
	EXPECT_NEAR(ray.origin.x, camera.transform.origin.x, 0.0001);
	EXPECT_NEAR(ray.origin.y, camera.transform.origin.y, 0.0001);
	EXPECT_NEAR(ray.origin.z, camera.transform.origin.z, 0.0001);
	EXPECT_NEAR(ray.direction.x, 2.0 / expected_length, 0.0002);
	EXPECT_NEAR(ray.direction.y, 1.0 / expected_length, 0.0002);
	EXPECT_NEAR(ray.direction.z, -5.0 / expected_length, 0.0002);
}

TEST(QdrModelingRuntimeToolViewportProjectionTests, OrthographicViewportProjectionPreservesOffCenterRayOriginRoundTrip) {
	quader_editor_core::ViewportCameraState camera = front_camera();
        camera.projection = quader_editor_core::CameraProjection::Orthographic;
        camera.transform.origin = { 10.0F, 2.0F, 30.0F };
	camera.viewport_size = { 800.0F, 400.0F };
	camera.orthographic_size = 8.0;

	const quader::QVec3 point { 14.0F, 0.0F, 20.0F };
	quader::QVec2 screen;
	EXPECT_TRUE(quader_editor_core::project_viewport_point(camera, point, screen));
	EXPECT_NEAR(screen.x, 600.0F, 0.001F);
	EXPECT_NEAR(screen.y, 300.0F, 0.001F);

	const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(camera, screen);
	EXPECT_NEAR(ray.origin.x, 14.0F, 0.001F);
	EXPECT_NEAR(ray.origin.y, 0.0F, 0.001F);
	EXPECT_NEAR(ray.origin.z, 30.0F, 0.001F);
	EXPECT_NEAR(ray.direction.x, 0.0F, 0.0001F);
	EXPECT_NEAR(ray.direction.y, 0.0F, 0.0001F);
	EXPECT_NEAR(ray.direction.z, -1.0F, 0.0001F);
}

TEST(QdrModelingRuntimeToolViewportProjectionTests,
	 AffinePerspectiveCameraPreservesWorldProjectionAndLocalRayRoundTrip) {
	const quader_editor_core::ViewportCameraState world_camera = front_camera();
	const quader_editor_core::ViewportCameraState local_camera =
		affine_front_camera();
	const quader::QVec3 local_point { 0.25F, 0.8F, 1.0F };
	const quader::QVec3 world_point = world_from_test_object(local_point);

	quader::QVec2 world_screen;
	quader::QVec2 local_screen;
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		world_camera, world_point, world_screen));
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		local_camera, local_point, local_screen));
	EXPECT_NEAR(local_screen.x, world_screen.x, 0.001F);
	EXPECT_NEAR(local_screen.y, world_screen.y, 0.001F);

	const quader_poly::Ray local_ray =
		quader_editor_core::ray_from_viewport_screen_point(local_camera,
			local_screen);
	const quader::QVec3 expected_direction =
		quader::normalize(local_point - local_camera.transform.origin);
	EXPECT_NEAR(local_ray.direction.x, expected_direction.x, 0.0001F);
	EXPECT_NEAR(local_ray.direction.y, expected_direction.y, 0.0001F);
	EXPECT_NEAR(local_ray.direction.z, expected_direction.z, 0.0001F);
}

TEST(QdrModelingRuntimeToolViewportProjectionTests,
	 AffineOrthographicCameraPreservesWorldProjectionAndLocalRayRoundTrip) {
	quader_editor_core::ViewportCameraState world_camera = front_camera();
	world_camera.projection = quader_editor_core::CameraProjection::Orthographic;
	world_camera.viewport_size = { 800.0F, 400.0F };
	world_camera.orthographic_size = 8.0;
	quader_editor_core::ViewportCameraState local_camera = affine_front_camera();
	local_camera.projection = quader_editor_core::CameraProjection::Orthographic;
	local_camera.viewport_size = world_camera.viewport_size;
	local_camera.orthographic_size = world_camera.orthographic_size;
	const quader::QVec3 local_point { 0.25F, 0.8F, 1.0F };
	const quader::QVec3 world_point = world_from_test_object(local_point);

	quader::QVec2 world_screen;
	quader::QVec2 local_screen;
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		world_camera, world_point, world_screen));
	ASSERT_TRUE(quader_editor_core::project_viewport_point(
		local_camera, local_point, local_screen));
	EXPECT_NEAR(local_screen.x, world_screen.x, 0.001F);
	EXPECT_NEAR(local_screen.y, world_screen.y, 0.001F);

	const quader_poly::Ray local_ray =
		quader_editor_core::ray_from_viewport_screen_point(local_camera,
			local_screen);
	EXPECT_NEAR(local_ray.origin.x, local_point.x, 0.0001F);
	EXPECT_NEAR(local_ray.origin.y, local_point.y, 0.0001F);
	EXPECT_NEAR(local_ray.origin.z, 2.0F, 0.0001F);
	EXPECT_NEAR(local_ray.direction.x, 0.0F, 0.0001F);
	EXPECT_NEAR(local_ray.direction.y, 0.0F, 0.0001F);
	EXPECT_NEAR(local_ray.direction.z, -1.0F, 0.0001F);
}

TEST(QdrModelingRuntimeToolViewportProjectionTests,
     PreparedPerspectiveCameraKeepsFrozenProjectionAndRays) {
  auto camera = front_camera();
  camera.transform.origin = {};
  camera.viewport_size = {800.0f, 400.0f};
  camera.fov_degrees = 90.0;
  const quader_editor_core::PreparedViewportProjection prepared(camera);
  camera.transform.origin = {100.0f, 200.0f, 300.0f};
  camera.transform.x_axis = {};
  camera.viewport_size = {1.0f, 1.0f};
  camera.fov_degrees = 1.0;

  quader::QVec2 screen{};
  for (int sample = 1; sample <= 4; ++sample) {
    const auto distance = static_cast<float>(sample);
    ASSERT_TRUE(prepared.project_point({4.0f * distance, distance, -2.0f * distance}, screen));
    EXPECT_NEAR(screen.x, 800.0f, 0.0001f);
    EXPECT_NEAR(screen.y, 100.0f, 0.0001f);
    EXPECT_FALSE(prepared.point_is_behind_camera({0.0f, 0.0f, -distance}));
  }
  const auto ray = prepared.ray_from_screen_point({800.0f, 100.0f});
  const double length = std::sqrt(21.0);
  EXPECT_FLOAT_EQ(ray.origin.x, 0.0f);
  EXPECT_FLOAT_EQ(ray.origin.y, 0.0f);
  EXPECT_FLOAT_EQ(ray.origin.z, 0.0f);
  EXPECT_NEAR(ray.direction.x, 4.0 / length, 0.000001);
  EXPECT_NEAR(ray.direction.y, 1.0 / length, 0.000001);
  EXPECT_NEAR(ray.direction.z, -2.0 / length, 0.000001);
  EXPECT_TRUE(prepared.point_is_behind_camera({0.0f, 0.0f, 1.0f}));
}

TEST(QdrModelingRuntimeToolViewportProjectionTests,
     PreparedAffineCameraPreservesShearedReflectedProjectionAndRays) {
  auto camera = front_camera();
  camera.affine_model_space = true;
  camera.transform.origin = {3.0f, -4.0f, 2.0f};
  camera.transform.x_axis = {-2.0f, 1.0f, 0.0f};
  camera.transform.y_axis = {0.0f, 0.5f, 0.25f};
  camera.transform.z_axis = {0.0f, 0.0f, 2.0f};
  camera.viewport_size = {800.0f, 400.0f};
  camera.fov_degrees = 90.0;
  camera.orthographic_size = 8.0;
  const quader::QVec3 point{4.0f, -2.0f, 0.0f};
  quader::QVec2 screen{};
  const quader_editor_core::PreparedViewportProjection perspective(camera);
  ASSERT_TRUE(perspective.project_point(point, screen));
  EXPECT_NEAR(screen.x, 400.0f, 0.0001f);
  EXPECT_NEAR(screen.y, 175.0f, 0.0001f);
  const auto perspective_ray = perspective.ray_from_screen_point(screen);
  EXPECT_FLOAT_EQ(perspective_ray.origin.x, 3.0f);
  EXPECT_FLOAT_EQ(perspective_ray.origin.y, -4.0f);
  EXPECT_FLOAT_EQ(perspective_ray.origin.z, 2.0f);
  EXPECT_NEAR(perspective_ray.direction.x, 1.0 / 3.0, 0.000001);
  EXPECT_NEAR(perspective_ray.direction.y, 2.0 / 3.0, 0.000001);
  EXPECT_NEAR(perspective_ray.direction.z, -2.0 / 3.0, 0.000001);

  camera.projection = quader_editor_core::CameraProjection::Orthographic;
  const quader_editor_core::PreparedViewportProjection orthographic(camera);
  ASSERT_TRUE(orthographic.project_point(point, screen));
  EXPECT_NEAR(screen.x, 400.0f, 0.0001f);
  EXPECT_NEAR(screen.y, 175.0f, 0.0001f);
  const auto orthographic_ray = orthographic.ray_from_screen_point(screen);
  EXPECT_FLOAT_EQ(orthographic_ray.origin.x, 3.5f);
  EXPECT_FLOAT_EQ(orthographic_ray.origin.y, -3.0f);
  EXPECT_FLOAT_EQ(orthographic_ray.origin.z, 2.0f);
  const double length = std::sqrt(21.0);
  EXPECT_NEAR(orthographic_ray.direction.x, 1.0 / length, 0.000001);
  EXPECT_NEAR(orthographic_ray.direction.y, 2.0 / length, 0.000001);
  EXPECT_NEAR(orthographic_ray.direction.z, -4.0 / length, 0.000001);
  EXPECT_FALSE(orthographic.point_is_behind_camera({3.0f, -4.0f, 100.0f}));
}

TEST(QdrModelingRuntimeToolViewportProjectionTests,
     PreparedCameraPreservesNearThresholdAndNonfinitePointRejection) {
  auto camera = front_camera();
  camera.transform.origin = {};
  const quader_editor_core::PreparedViewportProjection prepared(camera);
  constexpr float kNearDistance = 0.0001f;
  quader::QVec2 screen{17.0f, 19.0f};
  EXPECT_FALSE(prepared.project_point({0.0f, 0.0f, -kNearDistance}, screen));
  EXPECT_FLOAT_EQ(screen.x, 17.0f);
  EXPECT_FLOAT_EQ(screen.y, 19.0f);
  EXPECT_TRUE(prepared.point_is_behind_camera({0.0f, 0.0f, -kNearDistance}));
  const float visible_distance = std::nextafter(kNearDistance, 1.0f);
  ASSERT_TRUE(prepared.project_point({0.0f, 0.0f, -visible_distance}, screen));
  EXPECT_FLOAT_EQ(screen.x, 400.0f);
  EXPECT_FLOAT_EQ(screen.y, 300.0f);
  EXPECT_FALSE(prepared.point_is_behind_camera({0.0f, 0.0f, -visible_distance}));
  EXPECT_FALSE(prepared.project_point(
      {std::numeric_limits<float>::infinity(), 0.0f, -1.0f}, screen));
  EXPECT_FALSE(prepared.project_point(
      {0.0f, std::numeric_limits<float>::quiet_NaN(), -1.0f}, screen));
}

TEST(QdrModelingRuntimeToolViewportProjectionTests,
     PreparedOrthographicCameraRetainsDegenerateAxisAndExtentFallbacks) {
  auto camera = front_camera();
  camera.projection = quader_editor_core::CameraProjection::Orthographic;
  camera.transform = {};
  camera.transform.x_axis = {};
  camera.transform.y_axis = {};
  camera.transform.z_axis = {};
  camera.viewport_size = {0.0f, -10.0f};
  camera.orthographic_size = 0.0;
  const quader_editor_core::PreparedViewportProjection prepared(camera);
  quader::QVec2 screen{};
  ASSERT_TRUE(prepared.project_point({0.0001f, -0.0001f, 100.0f}, screen));
  EXPECT_NEAR(screen.x, 1.0f, 0.000001f);
  EXPECT_NEAR(screen.y, 1.0f, 0.000001f);
  EXPECT_FALSE(prepared.point_is_behind_camera({0.0f, 0.0f, 100.0f}));
  const auto ray = prepared.ray_from_screen_point({1.0f, 1.0f});
  EXPECT_FLOAT_EQ(ray.origin.x, 0.0001f);
  EXPECT_FLOAT_EQ(ray.origin.y, -0.0001f);
  EXPECT_FLOAT_EQ(ray.origin.z, 0.0f);
  EXPECT_FLOAT_EQ(ray.direction.x, 0.0f);
  EXPECT_FLOAT_EQ(ray.direction.y, 0.0f);
  EXPECT_FLOAT_EQ(ray.direction.z, -1.0f);
  EXPECT_FALSE(prepared.project_point(
      {0.0f, std::numeric_limits<float>::infinity(), 0.0f}, screen));
}

TEST(QdrModelingRuntimeToolViewportProjectionTests,
     PreparedSingularAffineCameraKeepsProjectionSpecificRayFallbacks) {
  auto camera = front_camera();
  camera.affine_model_space = true;
  camera.transform.x_axis = {1.0f, 0.0f, 0.0f};
  camera.transform.y_axis = {2.0f, 0.0f, 0.0f};
  camera.transform.z_axis = {0.0f, 0.0f, 2.0f};
  const quader_editor_core::PreparedViewportProjection perspective(camera);
  const auto perspective_ray = perspective.ray_from_screen_point({700.0f, 100.0f});
  EXPECT_FLOAT_EQ(perspective_ray.origin.z, 5.0f);
  EXPECT_FLOAT_EQ(perspective_ray.direction.x, 0.0f);
  EXPECT_FLOAT_EQ(perspective_ray.direction.y, 0.0f);
  EXPECT_FLOAT_EQ(perspective_ray.direction.z, -2.0f);

  camera.projection = quader_editor_core::CameraProjection::Orthographic;
  const quader_editor_core::PreparedViewportProjection orthographic(camera);
  const auto orthographic_ray = orthographic.ray_from_screen_point({700.0f, 100.0f});
  EXPECT_FLOAT_EQ(orthographic_ray.origin.x, 0.0f);
  EXPECT_FLOAT_EQ(orthographic_ray.origin.y, 0.0f);
  EXPECT_FLOAT_EQ(orthographic_ray.origin.z, 5.0f);
  EXPECT_FLOAT_EQ(orthographic_ray.direction.x, 0.0f);
  EXPECT_FLOAT_EQ(orthographic_ray.direction.y, 0.0f);
  EXPECT_FLOAT_EQ(orthographic_ray.direction.z, 0.0f);
}

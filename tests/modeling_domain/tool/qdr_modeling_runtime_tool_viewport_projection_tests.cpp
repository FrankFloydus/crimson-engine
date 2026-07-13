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

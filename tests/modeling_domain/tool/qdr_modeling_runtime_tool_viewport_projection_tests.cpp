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
	camera.projection = quader_editor_core::CameraProjection::perspective;
	camera.transform.origin = { 0.0F, 0.0F, 5.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
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
	camera.projection = quader_editor_core::CameraProjection::orthographic;
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

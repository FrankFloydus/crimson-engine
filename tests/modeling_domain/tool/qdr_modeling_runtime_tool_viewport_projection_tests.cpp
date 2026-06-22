////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <editor/interactions/interaction_api.h>

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

TEST_CASE("viewport projection and ray construction round trip screen center", "[editor-core][tool][projection]")
{
	const quader_editor_core::ViewportCameraState camera = front_camera();
	quader_poly::Vec2 screen;
	REQUIRE(quader_editor_core::project_viewport_point(camera, { 0.0F, 0.0F, 0.0F }, screen));
	CHECK(screen.x == Catch::Approx(400.0F).margin(0.001F));
	CHECK(screen.y == Catch::Approx(300.0F).margin(0.001F));
	CHECK_FALSE(quader_editor_core::viewport_point_is_behind_camera(camera, { 0.0F, 0.0F, 0.0F }));
	CHECK(quader_editor_core::viewport_point_is_behind_camera(camera, { 0.0F, 0.0F, 7.0F }));

	const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(camera, { 400.0F, 300.0F });
	CHECK(ray.origin.z == Catch::Approx(5.0F));
	CHECK(ray.direction.x == Catch::Approx(0.0F).margin(0.0001F));
	CHECK(ray.direction.y == Catch::Approx(0.0F).margin(0.0001F));
	CHECK(ray.direction.z == Catch::Approx(-1.0F).margin(0.0001F));
}

TEST_CASE("perspective viewport projection preserves off-center ray round trip", "[editor-core][tool][projection]")
{
	const quader_editor_core::ViewportCameraState camera = front_camera();
	const quader_poly::Vec3 point { 2.0F, 1.0F, 0.0F };
	quader_poly::Vec2 screen;
	REQUIRE(quader_editor_core::project_viewport_point(camera, point, screen));
	CHECK(screen.x > 400.0F);
	CHECK(screen.y < 300.0F);

	const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(camera, screen);
	const double expected_length = std::sqrt(2.0 * 2.0 + 1.0 * 1.0 + 5.0 * 5.0);
	CHECK(ray.origin.x == Catch::Approx(camera.transform.origin.x));
	CHECK(ray.origin.y == Catch::Approx(camera.transform.origin.y));
	CHECK(ray.origin.z == Catch::Approx(camera.transform.origin.z));
	CHECK(ray.direction.x == Catch::Approx(2.0 / expected_length).margin(0.0002));
	CHECK(ray.direction.y == Catch::Approx(1.0 / expected_length).margin(0.0002));
	CHECK(ray.direction.z == Catch::Approx(-5.0 / expected_length).margin(0.0002));
}

TEST_CASE("orthographic viewport projection preserves off-center ray origin round trip", "[editor-core][tool][projection]")
{
	quader_editor_core::ViewportCameraState camera = front_camera();
	camera.projection = quader_editor_core::CameraProjection::orthographic;
	camera.transform.origin = { 10.0F, 2.0F, 30.0F };
	camera.viewport_size = { 800.0F, 400.0F };
	camera.orthographic_size = 8.0;

	const quader_poly::Vec3 point { 14.0F, 0.0F, 20.0F };
	quader_poly::Vec2 screen;
	REQUIRE(quader_editor_core::project_viewport_point(camera, point, screen));
	CHECK(screen.x == Catch::Approx(600.0F).margin(0.001F));
	CHECK(screen.y == Catch::Approx(300.0F).margin(0.001F));

	const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(camera, screen);
	CHECK(ray.origin.x == Catch::Approx(14.0F).margin(0.001F));
	CHECK(ray.origin.y == Catch::Approx(0.0F).margin(0.001F));
	CHECK(ray.origin.z == Catch::Approx(30.0F).margin(0.001F));
	CHECK(ray.direction.x == Catch::Approx(0.0F).margin(0.0001F));
	CHECK(ray.direction.y == Catch::Approx(0.0F).margin(0.0001F));
	CHECK(ray.direction.z == Catch::Approx(-1.0F).margin(0.0001F));
}

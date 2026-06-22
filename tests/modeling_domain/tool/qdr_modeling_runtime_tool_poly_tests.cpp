////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <editor/modeling/runtime/editor_runtime.h>
#include <editor/interactions/interaction_api.h>

TEST_CASE("viewport grid point placement can snap or preserve raw positions", "[editor-core][tool][poly]")
{
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;

	const quader_editor_core::CutGridPointResult snapped =
		quader_editor_core::viewport_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5,
			true);
	REQUIRE(snapped.ok);
	CHECK(snapped.position.x == Catch::Approx(0.5F));
	CHECK(snapped.position.y == Catch::Approx(0.0F));
	CHECK(snapped.position.z == Catch::Approx(1.5F));

	const quader_editor_core::CutGridPointResult unsnapped =
		quader_editor_core::viewport_grid_point_from_ray(
			camera,
			{ { 0.49F, 2.0F, 1.51F }, { 0.0F, -1.0F, 0.0F } },
			0.5,
			false);
	REQUIRE(unsnapped.ok);
	CHECK(unsnapped.position.x == Catch::Approx(0.49F));
	CHECK(unsnapped.position.y == Catch::Approx(0.0F));
	CHECK(unsnapped.position.z == Catch::Approx(1.51F));
}

TEST_CASE("poly tool previews placed vertices, straightened hover line, and closure", "[editor-core][tool][poly]")
{
	quader_editor_core::PolyToolState state;
	state.active = true;

	REQUIRE(quader_editor_core::update_poly_hover(state, { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, false, 1.0, true).ok);
	REQUIRE(quader_editor_core::accept_poly_current_point(state).ok);
	REQUIRE(quader_editor_core::update_poly_hover(state, { 1.0F, 0.0F, 0.4F }, { 0.0F, 1.0F, 0.0F }, true, 1.0, true).ok);
	CHECK(state.current_point.x == Catch::Approx(1.0F));
	CHECK(state.current_point.z == Catch::Approx(0.0F));
	REQUIRE(quader_editor_core::accept_poly_current_point(state).ok);
	REQUIRE(quader_editor_core::update_poly_hover(state, { 1.0F, 0.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }, false, 1.0, true).ok);
	REQUIRE(quader_editor_core::accept_poly_current_point(state).ok);

	REQUIRE(state.points.size() == 3);
	CHECK(quader_editor_core::poly_can_commit(state));
	quader_editor_core::PolyToolPreview preview = quader_editor_core::poly_tool_preview(state);
	CHECK(preview.active);
	CHECK(preview.finalizable);
	CHECK(preview.point_count == 3U);
	CHECK(preview.line_count == 2U);

	REQUIRE(quader_editor_core::update_poly_hover(state, { 0.005F, 0.0F, 0.005F }, { 0.0F, 1.0F, 0.0F }, false, 1.0, false).ok);
	REQUIRE(quader_editor_core::accept_poly_current_point(state).ok);
	CHECK(state.closed);
	CHECK_FALSE(state.has_current);
	preview = quader_editor_core::poly_tool_preview(state);
	CHECK(preview.closed);
	CHECK(preview.line_count == 3U);
}

TEST_CASE("poly tool commits a polygon object through editor history", "[editor-core][tool][poly]")
{
	quader_editor_core::EditorSession session;
	session.set_objects({});
	quader_editor_core::PolyToolState state;
	state.active = true;
	state.face_normal = { 0.0F, 1.0F, 0.0F };
	state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 1.0F },
		{ 0.0F, 0.0F, 1.0F },
	};
	quader_editor_core::rebuild_poly_overlay(state);

	const quader_editor_core::CommandResult result = session.commit_poly_tool(state);
	REQUIRE(result.ok);
	CHECK(result.changed);
	REQUIRE(session.objects().size() == 1U);
	CHECK(session.objects()[0].selected);
	CHECK(session.objects()[0].document.vertices.size() == 4U);
	CHECK(session.objects()[0].document.faces.size() == 1U);
	CHECK_FALSE(state.active);

	quader_editor_core::PolyToolState short_state;
	short_state.active = true;
	short_state.points = {
		{ 0.0F, 0.0F, 0.0F },
		{ 1.0F, 0.0F, 0.0F },
	};
	const quader_editor_core::CommandResult short_result = session.commit_poly_tool(short_state);
	CHECK_FALSE(short_result.ok);
	CHECK(short_state.points.size() == 2U);
}

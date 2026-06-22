////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <modeling/interactions/interaction_api.h>

TEST_CASE("extend drag uses face normal and precision snapping", "[editor-core][tool][extend]")
{
	quader_editor_core::ExtendBeginInput input;
	input.source_object_id = 7;
	input.source_face_index = 3;
	input.face_center = { 1.0F, 2.0F, 3.0F };
	input.face_normal = { 0.0F, 2.0F, 0.0F };
	input.screen_axis = { 0.0F, -4.0F };

	const quader_editor_core::ExtendStateResult begin = quader_editor_core::begin_extend_drag(input);
	REQUIRE(begin.ok);
	CHECK(begin.state.active);
	CHECK(begin.state.source_object_id == 7);
	CHECK(begin.state.source_face_index == 3);
	CHECK(begin.state.face_normal.y == Catch::Approx(1.0F));
	CHECK(begin.state.screen_axis.y == Catch::Approx(-1.0F));

	quader_editor_core::ExtendToolState state = begin.state;
	const quader_editor_core::ExtendStateResult preview =
		quader_editor_core::preview_extend_from_distance(state, 2.0, true, true, 0.25);
	REQUIRE(preview.ok);
	CHECK(preview.changed);
	CHECK(preview.state.preview_distance == Catch::Approx(0.5));
	CHECK(preview.state.preview_delta.y == Catch::Approx(0.5F));
}

TEST_CASE("extend drag falls back screen axes and builds projected handles", "[editor-core][tool][extend]")
{
	quader_editor_core::ExtendBeginInput input;
	input.source_object_id = 6;
	input.source_face_index = 1;
	input.face_normal = { 0.0F, 1.0F, 0.0F };
	input.screen_axis = {};

	quader_editor_core::ExtendStateResult state = quader_editor_core::begin_extend_drag(input);
	REQUIRE(state.ok);
	CHECK(state.state.screen_axis.x == Catch::Approx(1.0F));
	CHECK(state.state.screen_axis.y == Catch::Approx(0.0F));

	state = quader_editor_core::preview_extend_from_drag_pixels(state.state, 96.0, false, true, 2.0);
	REQUIRE(state.ok);
	CHECK(state.state.preview_distance == Catch::Approx(4.0));

	CHECK_FALSE(quader_editor_core::begin_extend_drag({ 6, 1, {}, {}, { 1.0F, 0.0F } }).ok);
	CHECK(quader_editor_core::extend_handle_axis_for_normal({ -0.9F, 0.1F, 0.0F }).x == Catch::Approx(-1.0F));

	quader_editor_core::ExtendHandlePrimitiveInput handle_input;
	handle_input.normal = { 0.0F, 1.0F, 0.0F };
	handle_input.screen_center = { 100.0F, 120.0F };
	handle_input.has_projected_axis_delta = true;
	handle_input.projected_axis_delta = { 0.0F, -10.0F };

	quader_editor_core::ExtendHandlePrimitives handle =
		quader_editor_core::build_extend_handle_primitives(handle_input);
	REQUIRE(handle.ok);
	CHECK(handle.stick_visible);
	CHECK(handle.screen_axis.y == Catch::Approx(-1.0F));
	CHECK(handle.handle_center.y == Catch::Approx(64.0F));

	handle_input.normal = { 0.0F, 0.0F, -1.0F };
	handle_input.has_projected_axis_delta = false;
	handle_input.projected_axis_delta = {};
	handle = quader_editor_core::build_extend_handle_primitives(handle_input);
	REQUIRE(handle.ok);
	CHECK_FALSE(handle.stick_visible);
	CHECK(handle.handle_center.x == Catch::Approx(handle_input.screen_center.x));
	CHECK(handle.handle_center.y == Catch::Approx(handle_input.screen_center.y));
}

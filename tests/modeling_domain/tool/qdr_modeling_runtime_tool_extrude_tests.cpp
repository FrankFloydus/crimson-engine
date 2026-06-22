////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <editor/interactions/interaction_api.h>

TEST_CASE("extrude drag reports preview mode and cut count policy", "[editor-core][tool][extrude]")
{
	quader_editor_core::ExtrudeBeginInput input;
	input.source_object_id = 4;
	input.source_face_index = 1;
	input.face_normal = { 0.0F, 0.0F, 1.0F };
	input.face_vertices = {
		{ -1.0F, -1.0F, 0.0F },
		{ 1.0F, -1.0F, 0.0F },
		{ 1.0F, 1.0F, 0.0F },
		{ -1.0F, 1.0F, 0.0F },
	};
	input.face_vertex_indices = { 0, 1, 2, 3 };

	const quader_editor_core::ExtrudeStateResult begin = quader_editor_core::begin_extrude_drag(input);
	REQUIRE(begin.ok);
	quader_editor_core::ExtrudeToolState state = begin.state;

	REQUIRE(quader_editor_core::preview_extrude_from_distance(state, 3.0, false, true, 1.0).ok);
	CHECK(state.preview_distance == Catch::Approx(3.0));
	CHECK(state.max_cut_count == 2);

	const quader_editor_core::ExtrudeStateResult cuts =
		quader_editor_core::adjust_extrude_cut_count(state, 5, true, 1.0);
	REQUIRE(cuts.ok);
	CHECK(cuts.changed);
	CHECK(cuts.state.requested_cut_count == 2);
	CHECK(cuts.state.active_cut_count == 2);

	quader_editor_core::set_extrude_preview_valid(state, true);
	const quader_editor_core::ExtrudePreviewOperation operation = quader_editor_core::extrude_preview_operation(state);
	REQUIRE(operation.ok);
	CHECK(operation.preview_mode == quader_editor_core::ExtrudePreviewMode::positive);
	CHECK_FALSE(operation.clear_preview_meshes);

	REQUIRE(quader_editor_core::preview_extrude_from_distance(state, -1.0, false, true, 1.0).ok);
	const quader_editor_core::ExtrudePreviewOperation negative_operation = quader_editor_core::extrude_preview_operation(state);
	CHECK(negative_operation.preview_mode == quader_editor_core::ExtrudePreviewMode::negative);
	CHECK(negative_operation.clear_preview_meshes);
}

TEST_CASE("extrude drag pixels and grid cuts clamp to preview capacity", "[editor-core][tool][extrude]")
{
	quader_editor_core::ExtrudeBeginInput input;
	input.source_object_id = 7;
	input.source_face_index = 2;
	input.face_normal = { 0.0F, 1.0F, 0.0F };
	input.face_center = {};
	input.screen_axis = { 0.0F, 1.0F };

	quader_editor_core::ExtrudeStateResult result = quader_editor_core::begin_extrude_drag(input);
	REQUIRE(result.ok);
	quader_editor_core::ExtrudeToolState state = result.state;

	result = quader_editor_core::preview_extrude_from_drag_pixels(state, 96.0, false, true, 64.0);
	REQUIRE(result.ok);
	CHECK(result.state.preview_distance == Catch::Approx(128.0));
	CHECK(result.state.active_cut_count == 0);

	result = quader_editor_core::adjust_extrude_cut_count(result.state, 5, true, 64.0);
	REQUIRE(result.ok);
	CHECK(result.changed);
	CHECK(result.state.requested_cut_count == 1);
	CHECK(result.state.active_cut_count == 1);

	const quader_editor_core::ExtrudePreviewStatus status =
		quader_editor_core::extrude_preview_status(result.state, true);
	CHECK(status.show_cut_status);
	CHECK(status.active_cut_count == 1);
	CHECK(status.max_cut_count == 1);
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <modeling/interactions/interaction_api.h>

TEST_CASE("commit cut tool applies plane clipping through editor history", "[operation][commit_cut_tool]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	session.set_selected_object_ids({ 1 }, 1);

	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 0.0F, 1.0F };
	state.target_object_ids = { 1 };
	state.mode = quader_editor_core::CutMode::keep_both;

	const quader_editor_core::CommandResult result = session.commit_cut_tool(state);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 2U);
	CHECK(session.selection_mode() == quader_editor_core::SelectionMode::face);
	CHECK(session.history_status().can_undo);
	CHECK_FALSE(quader_editor_core::cut_is_active(state));
	REQUIRE(session.undo().applied);
	REQUIRE(session.objects().size() == 1U);
}

TEST_CASE("commit cut tool rejects incomplete state predictably", "[operation][commit_cut_tool]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::CutToolState state;
	state.points = { { 0.0F, 0.0F, 0.0F } };
	state.target_object_ids = { 1 };

	const quader_editor_core::CommandResult result = session.commit_cut_tool(state);

	REQUIRE_FALSE(result.ok);
	REQUIRE_FALSE(result.changed);
	CHECK_FALSE(quader_editor_core::cut_is_active(state));
}

TEST_CASE("commit cut tool preserves placed line when no cut targets are selected", "[operation][commit_cut_tool]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 0.0F, 1.0F };

	const quader_editor_core::CommandResult result = session.commit_cut_tool(state);

	REQUIRE_FALSE(result.ok);
	REQUIRE_FALSE(result.changed);
	CHECK(quader_editor_core::cut_is_active(state));
	CHECK(state.points.size() == 2);
}

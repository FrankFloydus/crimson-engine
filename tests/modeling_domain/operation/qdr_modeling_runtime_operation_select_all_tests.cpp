////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

TEST_CASE("select all operation selects every object in object mode", "[operation][select_all]")
{
	SECTION("object mode selects every object")
	{
		quader_editor_core::EditorSession session;
		REQUIRE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		REQUIRE(session.create_box_from_bounds({ 3.0F, -1.0F, -1.0F }, { 5.0F, 1.0F, 1.0F }).changed);
		session.clear_selection();
		REQUIRE(session.selected_object_ids().empty());

		const quader_editor_core::CommandResult result = session.select_all();

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.selected_object_ids().size() == 2U);
		REQUIRE(session.has_selection());
	}

	SECTION("face mode selects every face on the active object")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		session.set_selection_mode(quader_editor_core::SelectionMode::face);
		REQUIRE_FALSE(session.has_selection());

		const quader_editor_core::CommandResult result = session.select_all();

		REQUIRE(result.ok);
		REQUIRE(result.changed);
		REQUIRE(session.selection_mode() == quader_editor_core::SelectionMode::face);
		REQUIRE(session.objects().front().selected);
		REQUIRE(session.objects().front().selection.faces.size() == session.objects().front().document.faces.size());
		REQUIRE(session.active_object_id() == session.objects().front().id);
	}

	SECTION("empty sessions are no-ops")
	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult result = session.select_all();

		REQUIRE(result.ok);
		REQUIRE_FALSE(result.changed);
		REQUIRE_FALSE(session.has_selection());
	}
}

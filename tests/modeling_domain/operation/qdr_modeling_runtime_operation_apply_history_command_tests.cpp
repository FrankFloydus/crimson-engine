////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

TEST_CASE("apply history command records only successful content changes", "[operation][apply_history_command]")
{
	SECTION("successful content commands push undo state")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		REQUIRE_FALSE(session.history_status().can_undo);

		const quader_editor_core::CommandResult command =
			session.apply_history_command([](quader_editor_core::EditorSession& editor) {
				return editor.translate_selected({ 2.0F, 0.0F, 0.0F });
			});

		REQUIRE(command.ok);
		REQUIRE(command.changed);
		REQUIRE(session.history_status().can_undo);
	}

	SECTION("failed history commands restore their before state and do not push undo")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::Vec3 position_before = session.objects().front().document.vertices.front().position;

		const quader_editor_core::CommandResult command =
			session.apply_history_command([](quader_editor_core::EditorSession& editor) {
				editor.translate_selected({ 2.0F, 0.0F, 0.0F });
				return quader_editor_core::CommandResult { false, false, "forced failure" };
			});

		REQUIRE_FALSE(command.ok);
		REQUIRE_FALSE(command.changed);
		REQUIRE_FALSE(session.history_status().can_undo);
		REQUIRE(qdr_test::vec_nearly_equal(session.objects().front().document.vertices.front().position, position_before));
	}

	SECTION("new history after undo clears redo")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		REQUIRE(session.apply_history_command([](quader_editor_core::EditorSession& editor) {
			return editor.translate_selected({ 2.0F, 0.0F, 0.0F });
		}).changed);
		REQUIRE(session.undo().applied);
		REQUIRE(session.history_status().can_redo);

		REQUIRE(session.apply_history_command([](quader_editor_core::EditorSession& editor) {
			return editor.translate_selected({ 0.0F, 3.0F, 0.0F });
		}).changed);

		REQUIRE(session.history_status().can_undo);
		REQUIRE_FALSE(session.history_status().can_redo);
	}
}

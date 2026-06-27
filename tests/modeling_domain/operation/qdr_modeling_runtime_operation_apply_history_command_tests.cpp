////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationApplyHistoryCommandTests, ApplyHistoryCommandRecordsOnlySuccessfulContentChangesSuccessfulContentCommandsPushUndoState) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		EXPECT_FALSE(session.history_status().can_undo);

		const quader_editor_core::CommandResult command =
			session.apply_history_command([](quader_editor_core::EditorSession& editor) {
				return editor.translate_selected({ 2.0F, 0.0F, 0.0F });
			});

		EXPECT_TRUE(command.ok);
		EXPECT_TRUE(command.changed);
		EXPECT_TRUE(session.history_status().can_undo);
	}

	

	
}

TEST(QdrModelingRuntimeOperationApplyHistoryCommandTests, ApplyHistoryCommandRecordsOnlySuccessfulContentChangesFailedHistoryCommandsRestoreTheirBeforeStateAndDoNotPushUndo) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader::QVec3 position_before = session.objects().front().document.vertices.front().position;

		const quader_editor_core::CommandResult command =
			session.apply_history_command([](quader_editor_core::EditorSession& editor) {
				editor.translate_selected({ 2.0F, 0.0F, 0.0F });
				return quader_editor_core::CommandResult { false, false, "forced failure" };
			});

		EXPECT_FALSE(command.ok);
		EXPECT_FALSE(command.changed);
		EXPECT_FALSE(session.history_status().can_undo);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(session.objects().front().document.vertices.front().position, position_before));
	}

	
}

TEST(QdrModelingRuntimeOperationApplyHistoryCommandTests, ApplyHistoryCommandRecordsOnlySuccessfulContentChangesNewHistoryAfterUndoClearsRedo) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		EXPECT_TRUE(session.apply_history_command([](quader_editor_core::EditorSession& editor) {
			return editor.translate_selected({ 2.0F, 0.0F, 0.0F });
		}).changed);
		EXPECT_TRUE(session.undo().applied);
		EXPECT_TRUE(session.history_status().can_redo);

		EXPECT_TRUE(session.apply_history_command([](quader_editor_core::EditorSession& editor) {
			return editor.translate_selected({ 0.0F, 3.0F, 0.0F });
		}).changed);

		EXPECT_TRUE(session.history_status().can_undo);
		EXPECT_FALSE(session.history_status().can_redo);
	}
}

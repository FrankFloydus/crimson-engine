////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationRedoTests, RedoOperationReappliesTheUndoneEditorSnapshotRedoReappliesAnUndoneContentCommand) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader::QVec3 position_before = session.objects().front().document.vertices.front().position;
		EXPECT_TRUE(session.apply_history_command([](quader_editor_core::EditorSession& editor) {
			return editor.translate_selected({ 2.0F, 0.0F, 0.0F });
		}).changed);
		EXPECT_TRUE(session.undo().applied);

		const quader_editor_core::HistoryRestoreResult redo = session.redo();

		EXPECT_TRUE(redo.applied);
		EXPECT_TRUE(session.history_status().can_undo);
		EXPECT_NEAR(session.objects().front().document.vertices.front().position.x, position_before.x + 2.0F, 0.0001);
	}

	
}

TEST(QdrModelingRuntimeOperationRedoTests, RedoOperationReappliesTheUndoneEditorSnapshotRedoOnAnEmptyStackReportsNoRestore) {
	

	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::HistoryRestoreResult redo = session.redo();

		EXPECT_FALSE(redo.applied);
	}
}

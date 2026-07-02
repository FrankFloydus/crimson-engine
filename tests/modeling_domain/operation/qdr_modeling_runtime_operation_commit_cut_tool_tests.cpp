////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/interactions/interaction_api.hpp>

TEST(QdrModelingRuntimeOperationCommitCutToolTests, CommitCutToolAppliesPlaneClippingThroughEditorHistory) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	session.set_selected_object_ids({ 1 }, 1);

	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 0.0F, 1.0F };
	state.target_object_ids = { 1 };
        state.mode = quader_editor_core::CutMode::KeepBoth;

        const quader_editor_core::CommandResult result = session.commit_cut_tool(state);

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
        EXPECT_TRUE(session.selection_mode() ==
                    quader_editor_core::SelectionMode::Face);
        EXPECT_TRUE(session.history_status().can_undo);
	EXPECT_FALSE(quader_editor_core::cut_is_active(state));
	EXPECT_TRUE(session.undo().applied);
	EXPECT_TRUE(session.objects().size() == 1U);
}

TEST(QdrModelingRuntimeOperationCommitCutToolTests, CommitCutToolRejectsIncompleteStatePredictably) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::CutToolState state;
	state.points = { { 0.0F, 0.0F, 0.0F } };
	state.target_object_ids = { 1 };

	const quader_editor_core::CommandResult result = session.commit_cut_tool(state);

	EXPECT_FALSE(result.ok);
	EXPECT_FALSE(result.changed);
	EXPECT_FALSE(quader_editor_core::cut_is_active(state));
}

TEST(QdrModelingRuntimeOperationCommitCutToolTests, CommitCutToolPreservesPlacedLineWhenNoCutTargetsAreSelected) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::CutToolState state;
	state.points = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.face_normal = { 0.0F, 0.0F, 1.0F };

	const quader_editor_core::CommandResult result = session.commit_cut_tool(state);

	EXPECT_FALSE(result.ok);
	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(quader_editor_core::cut_is_active(state));
	EXPECT_TRUE(state.points.size() == 2);
}

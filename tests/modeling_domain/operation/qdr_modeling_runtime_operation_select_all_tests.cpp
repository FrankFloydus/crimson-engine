////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationSelectAllTests, SelectAllOperationSelectsEveryObjectInObjectModeObjectModeSelectsEveryObject) {
	{
		quader_editor_core::EditorSession session;
		EXPECT_TRUE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		EXPECT_TRUE(session.create_box_from_bounds({ 3.0F, -1.0F, -1.0F }, { 5.0F, 1.0F, 1.0F }).changed);
		session.clear_selection();
		EXPECT_TRUE(session.selected_object_ids().empty());

		const quader_editor_core::CommandResult result = session.select_all();

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.selected_object_ids().size() == 2U);
		EXPECT_TRUE(session.has_selection());
	}

	

	
}

TEST(QdrModelingRuntimeOperationSelectAllTests, SelectAllOperationSelectsEveryObjectInObjectModeFaceModeSelectsEveryFaceOnTheActiveObject) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		session.set_selection_mode(quader_editor_core::SelectionMode::face);
		EXPECT_FALSE(session.has_selection());

		const quader_editor_core::CommandResult result = session.select_all();

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.selection_mode() == quader_editor_core::SelectionMode::face);
		EXPECT_TRUE(session.objects().front().selected);
		EXPECT_TRUE(session.objects().front().selection.faces.size() == session.objects().front().document.faces.size());
		EXPECT_TRUE(session.active_object_id() == session.objects().front().id);
	}

	
}

TEST(QdrModelingRuntimeOperationSelectAllTests, SelectAllOperationSelectsEveryObjectInObjectModeEmptySessionsAreNoOps) {
	

	

	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult result = session.select_all();

		EXPECT_TRUE(result.ok);
		EXPECT_FALSE(result.changed);
		EXPECT_FALSE(session.has_selection());
	}
}

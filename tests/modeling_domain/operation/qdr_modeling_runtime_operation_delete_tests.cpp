////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationDeleteTests, DeleteOperationRemovesSelectedEditorTargetsObjectModeRemovesSelectedObjectsAndNormalizesActiveObject) {
	{
		quader_editor_core::EditorSession session;
		EXPECT_TRUE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		EXPECT_TRUE(session.create_box_from_bounds({ 3.0F, -1.0F, -1.0F }, { 5.0F, 1.0F, 1.0F }).changed);
		const int remaining_id = session.objects().front().id;
		session.set_selected_object_ids({ session.objects().back().id }, session.objects().back().id);

		const quader_editor_core::CommandResult result = session.delete_selected();

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.objects().size() == 1U);
		EXPECT_TRUE(session.objects().front().id == remaining_id);
		EXPECT_FALSE(session.objects().front().selected);
		EXPECT_TRUE(session.active_object_id() == -1);
	}

	

	
}

TEST(QdrModelingRuntimeOperationDeleteTests, DeleteOperationRemovesSelectedEditorTargetsFaceModeClearsComponentSelectionAfterDeletingSelectedFaces) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId face_id = session.objects().front().document.faces.front().id;
		const std::size_t face_count_before = session.objects().front().document.faces.size();
                session.set_component_selection(
                    session.objects().front().id,
                    qdr_test::face_selection({face_id}, face_id),
                    quader_editor_core::SelectionMode::Face);

                const quader_editor_core::CommandResult result = session.delete_selected();

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before - 1U);
		EXPECT_FALSE(session.objects().front().selected);
		EXPECT_TRUE(session.objects().front().selection.empty());
		EXPECT_TRUE(session.active_object_id() == -1);
	}

	
}

TEST(QdrModelingRuntimeOperationDeleteTests, DeleteOperationRemovesSelectedEditorTargetsMissingSelectionReportsAnErrorWithoutChangingContent) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		session.clear_selection();
		const std::size_t object_count_before = session.objects().size();

		const quader_editor_core::CommandResult result = session.delete_selected();

		EXPECT_FALSE(result.ok);
		EXPECT_FALSE(result.changed);
		EXPECT_TRUE(session.objects().size() == object_count_before);
	}
}

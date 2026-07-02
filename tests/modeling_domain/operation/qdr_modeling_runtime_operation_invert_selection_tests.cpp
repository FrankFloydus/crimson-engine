////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationInvertSelectionTests, InvertSelectionOperationSwapsSelectedAndUnselectedObjectSetsObjectModeSwapsSelectedAndUnselectedObjectSets) {
	{
		quader_editor_core::EditorSession session;
		EXPECT_TRUE(session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F }).changed);
		EXPECT_TRUE(session.create_box_from_bounds({ 3.0F, -1.0F, -1.0F }, { 5.0F, 1.0F, 1.0F }).changed);
		const int first_id = session.objects()[0].id;
		const int second_id = session.objects()[1].id;
		session.set_selected_object_ids({ first_id }, first_id);

		const quader_editor_core::CommandResult result = session.invert_selection();

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		EXPECT_TRUE(session.selected_object_ids().front() == second_id);
	}

	

	
}

TEST(QdrModelingRuntimeOperationInvertSelectionTests, InvertSelectionOperationSwapsSelectedAndUnselectedObjectSetsVertexModeSelectsTheComplementOnTheActiveObject) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId selected_vertex = session.objects().front().document.vertices.front().id;
                session.set_component_selection(
                    session.objects().front().id,
                    qdr_test::vertex_selection({selected_vertex},
                                               selected_vertex),
                    quader_editor_core::SelectionMode::Vertex);

                const quader_editor_core::CommandResult result = session.invert_selection();

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.objects().front().selection.vertices.size() == session.objects().front().document.vertices.size() - 1U);
		EXPECT_FALSE(quader_poly::selection_contains(session.objects().front().selection, selected_vertex));
		EXPECT_TRUE(session.objects().front().selection.has_active);
	}

	
}

TEST(QdrModelingRuntimeOperationInvertSelectionTests, InvertSelectionOperationSwapsSelectedAndUnselectedObjectSetsInvertingAnEmptySessionIsANoOp) {
	

	

	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult result = session.invert_selection();

		EXPECT_TRUE(result.ok);
		EXPECT_FALSE(result.changed);
	}
}

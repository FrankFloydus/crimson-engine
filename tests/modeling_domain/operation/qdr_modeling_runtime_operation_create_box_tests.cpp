////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

TEST(QdrModelingRuntimeOperationCreateBoxTests, CreateBoxOperationAddsOneSelectedObjectWithCubeTopologyValidBoundsCreateASelectedRenderableCube) {
	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult result =
			session.create_box_from_bounds({ -2.0F, -1.0F, -3.0F }, { 2.0F, 1.0F, 3.0F });

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.objects().size() == 1U);
		EXPECT_TRUE(session.objects().front().selected);
                EXPECT_TRUE(session.selection_mode() ==
                            quader_editor_core::SelectionMode::Object);
                EXPECT_TRUE(session.active_object_id() == session.objects().front().id);
		EXPECT_TRUE(qdr_test::every_edge_is_closed(session.objects().front().document));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
	}

	

	
}

TEST(QdrModelingRuntimeOperationCreateBoxTests, CreateBoxOperationAddsOneSelectedObjectWithCubeTopologyReversedBoundsAreNormalizedAndPreviousSelectionIsCleared) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int first_id = session.objects().front().id;

		const quader_editor_core::CommandResult result =
			session.create_box_from_bounds({ 4.0F, 5.0F, 6.0F }, { 2.0F, 1.0F, 3.0F });

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(session.objects().size() == 2U);
		EXPECT_FALSE(session.objects().front().selected);
		EXPECT_TRUE(session.objects().back().selected);
		EXPECT_TRUE(session.objects().back().id != first_id);
		EXPECT_TRUE(session.active_object_id() == session.objects().back().id);
		const quader_editor_core::Bounds3 bounds = session.selection_bounds();
		EXPECT_FALSE(bounds.empty);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(bounds.min, { 2.0F, 1.0F, 3.0F }));
		EXPECT_TRUE(qdr_test::vec_nearly_equal(bounds.max, { 4.0F, 5.0F, 6.0F }));
	}

	
}

TEST(QdrModelingRuntimeOperationCreateBoxTests, CreateBoxOperationAddsOneSelectedObjectWithCubeTopologyDegenerateBoundsFailWithoutConsumingAnObjectId) {
	

	

	{
		quader_editor_core::EditorSession session;

		const quader_editor_core::CommandResult failed =
			session.create_box_from_bounds({ 1.0F, 1.0F, 1.0F }, { 1.0F, 2.0F, 3.0F });
		const quader_editor_core::CommandResult created =
			session.create_box_from_bounds({ -1.0F, -1.0F, -1.0F }, { 1.0F, 1.0F, 1.0F });

		EXPECT_FALSE(failed.ok);
		EXPECT_FALSE(failed.changed);
		EXPECT_TRUE(created.ok);
		EXPECT_TRUE(created.changed);
		EXPECT_TRUE(session.objects().size() == 1U);
		EXPECT_TRUE(session.objects().front().id == 1);
	}
}

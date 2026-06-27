////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>

#include <vector>

TEST(QdrModelingRuntimeOperationPasteTests, PasteOperationInsertsCopiedObjectsWithFreshIdsExplicitPasteTargetOffsetsTheNewSelectedObject) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const int source_id = session.objects().front().id;
		const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();

		const quader_editor_core::CommandResult paste = session.paste_objects(copied, quader::QVec3 { 4.0F, 0.0F, 0.0F });

		EXPECT_TRUE(paste.ok);
		EXPECT_TRUE(paste.changed);
		EXPECT_TRUE(session.objects().size() == 2U);
		EXPECT_TRUE(session.selected_object_ids().size() == 1U);
		EXPECT_TRUE(session.selected_object_ids().front() != source_id);
		EXPECT_TRUE(session.active_object_id() == session.selected_object_ids().front());
	}

	

	
}

TEST(QdrModelingRuntimeOperationPasteTests, PasteOperationInsertsCopiedObjectsWithFreshIdsPasteWithoutAnExplicitTargetCentersOnTheCurrentSelection) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();
		EXPECT_TRUE(session.translate_selected({ 5.0F, 0.0F, 0.0F }).changed);

		const quader_editor_core::CommandResult paste = session.paste_objects(copied, std::nullopt);

		EXPECT_TRUE(paste.ok);
		EXPECT_TRUE(paste.changed);
		EXPECT_TRUE(session.objects().size() == 2U);
		const quader_editor_core::Bounds3 selected_bounds = session.selection_bounds();
		EXPECT_FALSE(selected_bounds.empty);
		EXPECT_NEAR((selected_bounds.min.x + selected_bounds.max.x) * 0.5F, 5.0F, 0.0001);
	}

	
}

TEST(QdrModelingRuntimeOperationPasteTests, PasteOperationInsertsCopiedObjectsWithFreshIdsEmptyPasteReportsAnError) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const quader_editor_core::CommandResult paste = session.paste_objects({}, std::nullopt);

		EXPECT_FALSE(paste.ok);
		EXPECT_FALSE(paste.changed);
		EXPECT_TRUE(session.objects().size() == 1U);
	}
}

TEST(QdrModelingRuntimeOperationPasteTests, EditorRuntimeClipboardPasteRoutesThroughSessionPastePolicy) {
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	session.set_selected_object_ids({ 1 }, 1);
	quader_editor_core::EditorRuntime runtime;
	EXPECT_TRUE(runtime.copy_selected_objects_to_clipboard(session) == 1);

	const quader_editor_core::CommandResult paste =
		runtime.paste_clipboard(session, quader::QVec3 { 3.0F, 0.0F, 3.0F });

	EXPECT_TRUE(paste.ok);
	EXPECT_TRUE(paste.changed);
	EXPECT_TRUE(session.objects().size() == 2);
	EXPECT_TRUE(session.selected_object_ids().size() == 1);
	EXPECT_TRUE(session.selected_object_ids().front() == 2);
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>

#include <vector>

TEST(QdrModelingRuntimeOperationCopyTests, CopyOperationReturnsSelectedObjectCopiesOnlyInObjectModeSelectedObjectCopiesPreserveSourceContent) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();

		EXPECT_TRUE(copied.size() == 1U);
		EXPECT_TRUE(copied.front().id == session.objects().front().id);
		EXPECT_TRUE(copied.front().document.vertices.size() == session.objects().front().document.vertices.size());
	}

	
}

TEST(QdrModelingRuntimeOperationCopyTests, CopyOperationReturnsSelectedObjectCopiesOnlyInObjectModeComponentModeDoesNotProduceObjectClipboardCopies) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
                session.set_component_selection(
                    session.objects().front().id,
                    qdr_test::vertex_selection({vertex_id}, vertex_id),
                    quader_editor_core::SelectionMode::Vertex);

                const std::vector<quader_editor_core::PolygonObject> copied = session.selected_object_copies();

		EXPECT_TRUE(copied.empty());
	}
}

TEST(QdrModelingRuntimeOperationCopyTests, EditorRuntimeClipboardCopyCapturesSelectedObjectCopies) {
	quader_editor_core::EditorSession session;
	session.reset_to_default_scene();
	quader_editor_core::EditorRuntime runtime;

	EXPECT_TRUE(runtime.copy_selected_objects_to_clipboard(session) == 0);
	EXPECT_FALSE(runtime.clipboard_available());

	session.set_selected_object_ids({ 1 }, 1);
	EXPECT_TRUE(runtime.copy_selected_objects_to_clipboard(session) == 1);
	EXPECT_TRUE(runtime.clipboard_available());
	EXPECT_FALSE(runtime.clipboard_objects().empty());
	EXPECT_TRUE(runtime.clipboard_objects().front().id == 1);
	EXPECT_FALSE(runtime.clipboard_objects().front().selected);

	runtime.clear_clipboard();
	EXPECT_FALSE(runtime.clipboard_available());
	EXPECT_TRUE(runtime.status().clipboard_object_count == 0);
}

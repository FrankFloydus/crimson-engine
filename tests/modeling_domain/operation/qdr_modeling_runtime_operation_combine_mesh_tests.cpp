////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

using namespace quader_editor_core;

namespace {

void translate_document(quader_poly::Document& document, quader::QVec3 delta)
{
	for (quader_poly::Vertex& vertex : document.vertices) {
		vertex.position += delta;
	}
}

} // namespace

TEST(QdrModelingRuntimeOperationCombineMeshTests, CombineMeshMergesSelectedMeshObjectsIntoTheActiveSurvivor) {
	quader_poly::Document first = qdr_test::make_quad_document(2).document;
	quader_poly::Document second = qdr_test::make_quad_document(7).document;
	translate_document(second, { 4.0F, 0.0F, 0.0F });
	EditorSession session({ qdr_test::make_object(1, first), qdr_test::make_object(2, second) });
	session.set_selected_object_ids({ 1, 2 }, 1);

	const CommandResult result = session.combine_selected_meshes();

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.objects().front().id == 1);
	EXPECT_TRUE(session.objects().front().selected);
	EXPECT_TRUE(session.active_object_id() == 1);
	EXPECT_TRUE(session.selection_mode() == SelectionMode::object);
	EXPECT_TRUE(session.objects().front().document.vertices.size() == first.vertices.size() + second.vertices.size());
	EXPECT_TRUE(session.objects().front().document.faces.size() == first.faces.size() + second.faces.size());
	EXPECT_TRUE(std::ranges::any_of(session.objects().front().document.faces, [](const quader_poly::Face& face) {
		return face.material_slot == 7U;
	}));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
}

TEST(QdrModelingRuntimeOperationCombineMeshTests, CombineMeshRecordsOneUndoableHistoryEntryWhenCommittedThroughHistory) {
	EditorSession session({
		qdr_test::make_object(1, qdr_test::make_quad_document().document),
		qdr_test::make_object(2, qdr_test::make_quad_document().document),
	});
	session.set_selected_object_ids({ 1, 2 }, 2);

	const CommandResult result = session.apply_history_command([](EditorSession& editor_session) {
		return editor_session.combine_selected_meshes();
	});

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	EXPECT_TRUE(session.objects().front().id == 2);
	EXPECT_TRUE(session.history_status().undo_count == 1U);
	EXPECT_TRUE(session.undo().applied);
	EXPECT_TRUE(session.objects().size() == 2U);
	EXPECT_TRUE(session.active_object_id() == 2);
}

TEST(QdrModelingRuntimeOperationCombineMeshTests, CombineMeshRejectsNonObjectModeAndUndersizedSelections) {
	EditorSession session({ qdr_test::make_object(1, qdr_test::make_quad_document().document) });
	session.set_selected_object_ids({ 1 }, 1);

	EXPECT_FALSE(session.combine_selected_meshes().ok);

	quader_poly::Selection vertex_selection = qdr_test::vertex_selection({ session.objects().front().document.vertices.front().id });
	session.set_component_selection(1, vertex_selection, SelectionMode::vertex);
	const CommandResult result = session.combine_selected_meshes();

	EXPECT_FALSE(result.ok);
}

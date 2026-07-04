////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

namespace {

bool all_face_uvs_empty(const quader_poly::Document& document)
{
	for (const quader_poly::Face& face : document.faces) {
		if (!face.uvs.empty()) {
			return false;
		}
	}
	return true;
}

} // namespace

TEST(QdrModelingRuntimeOperationTranslateTests, TranslateOperationMovesSelectedObjectVerticesAndRecordsContentChangeObjectModeMovesEverySelectedObjectVertex) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader::QVec3 before = session.objects().front().document.vertices.front().position;

		const quader_editor_core::CommandResult result = session.translate_selected({ 2.0F, 0.0F, 0.0F });

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		const quader::QVec3 after = session.objects().front().document.vertices.front().position;
		EXPECT_NEAR(after.x, before.x + 2.0F, 0.0001);
		EXPECT_NEAR(after.y, before.y, 0.0001);
		EXPECT_NEAR(after.z, before.z, 0.0001);
	}

	

	
}

TEST(QdrModelingRuntimeOperationTranslateTests, VertexTranslateClearsStoredLoopUvsAndKeepsGeneratedProjectionProcedural) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	ASSERT_FALSE(session.objects().empty());
	quader_editor_core::PolygonObject& object = session.objects().front();
	quader_poly::ensure_face_uvs(object.document);
	ASSERT_FALSE(object.document.faces.empty());
	ASSERT_FALSE(all_face_uvs_empty(object.document));
	const int object_id = object.id;
	const quader_poly::ElementId selected_id = object.document.vertices.front().id;

	session.set_component_selection(
	    object_id,
	    qdr_test::vertex_selection({selected_id}, selected_id),
	    quader_editor_core::SelectionMode::Vertex);

	const quader_editor_core::CommandResult result =
	    session.translate_selected({0.0F, 3.0F, 0.0F});

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(all_face_uvs_empty(session.objects().front().document));
}

TEST(QdrModelingRuntimeOperationTranslateTests, VertexMatrixTransformClearsStoredLoopUvsAndKeepsGeneratedProjectionProcedural) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	ASSERT_FALSE(session.objects().empty());
	quader_editor_core::PolygonObject& object = session.objects().front();
	quader_poly::ensure_face_uvs(object.document);
	ASSERT_FALSE(object.document.faces.empty());
	ASSERT_FALSE(all_face_uvs_empty(object.document));
	const int object_id = object.id;
	const quader_poly::ElementId selected_id = object.document.vertices.front().id;

	session.set_component_selection(
	    object_id,
	    qdr_test::vertex_selection({selected_id}, selected_id),
	    quader_editor_core::SelectionMode::Vertex);

	quader_poly::Transform3 transform;
	transform.origin = {0.0F, 3.0F, 0.0F};
	const quader_editor_core::CommandResult result =
	    session.transform_selected(transform, {});

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(all_face_uvs_empty(session.objects().front().document));
}

TEST(QdrModelingRuntimeOperationTranslateTests, TranslateOperationMovesSelectedObjectVerticesAndRecordsContentChangeVertexModeMovesOnlySelectedVertices) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_poly::ElementId selected_id = session.objects().front().document.vertices.front().id;
		const quader::QVec3 selected_before = session.objects().front().document.vertices.front().position;
		const quader::QVec3 unselected_before = session.objects().front().document.vertices[1].position;
                session.set_component_selection(
                    session.objects().front().id,
                    qdr_test::vertex_selection({selected_id}, selected_id),
                    quader_editor_core::SelectionMode::Vertex);

                const quader_editor_core::CommandResult result = session.translate_selected({ 0.0F, 3.0F, 0.0F });

		EXPECT_TRUE(result.ok);
		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(session.objects().front().document.vertices.front().position, selected_before + quader::QVec3 { 0.0F, 3.0F, 0.0F }));
		EXPECT_TRUE(qdr_test::vec_nearly_equal(session.objects().front().document.vertices[1].position, unselected_before));
	}

	
}

TEST(QdrModelingRuntimeOperationTranslateTests, TranslateOperationMovesSelectedObjectVerticesAndRecordsContentChangeNoSelectedTargetIsAnUnchangedSuccess) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		session.clear_selection();

		const quader_editor_core::CommandResult result = session.translate_selected({ 1.0F, 0.0F, 0.0F });

		EXPECT_TRUE(result.ok);
		EXPECT_FALSE(result.changed);
	}
}

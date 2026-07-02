////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationTranslateSelectionTests, TranslateSelectionMovesOnlyVerticesReferencedByTheActiveSelectionFaceSelectionsTranslateEveryFaceVertex) {
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		const quader::QVec3 bottom_left_before = find_vertex(fixture.document, fixture.bottom_left)->position;
		Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

		const OperationResult result = translate_selection(fixture.document, selection, { 2.0F, -1.0F, 0.5F });

		EXPECT_TRUE(result.changed);
		const Vertex* bottom_left = find_vertex(fixture.document, fixture.bottom_left);
		EXPECT_TRUE(bottom_left != nullptr);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(bottom_left->position, bottom_left_before + quader::QVec3 { 2.0F, -1.0F, 0.5F }));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	}

	

	
}

TEST(QdrDocumentOperationTranslateSelectionTests, TranslateSelectionMovesOnlyVerticesReferencedByTheActiveSelectionVertexSelectionsLeaveUnselectedVerticesUntouched) {
	

	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		const quader::QVec3 selected_before = find_vertex(fixture.document, fixture.bottom_left)->position;
		const quader::QVec3 unselected_before = find_vertex(fixture.document, fixture.bottom_right)->position;
		Selection selection = qdr_test::vertex_selection({ fixture.bottom_left }, fixture.bottom_left);

		const OperationResult result = translate_selection(fixture.document, selection, { 0.0F, 3.0F, 0.0F });

		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.bottom_left)->position, selected_before + quader::QVec3 { 0.0F, 3.0F, 0.0F }));
		EXPECT_TRUE(qdr_test::vec_nearly_equal(find_vertex(fixture.document, fixture.bottom_right)->position, unselected_before));
	}

	
}

TEST(QdrDocumentOperationTranslateSelectionTests, TranslateSelectionMovesOnlyVerticesReferencedByTheActiveSelectionEmptySelectionsReportNoMutation) {
	

	

	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection;
                selection.mode = SelectionMode::Vertex;

                const OperationResult result = translate_selection(fixture.document, selection, { 1.0F, 0.0F, 0.0F });

		EXPECT_FALSE(result.changed);
		EXPECT_TRUE(result.message == "No polygon elements are selected.");
	}
}

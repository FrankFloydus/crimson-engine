////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationCombineFacesTests, CombineFacesRemovesASupportEdgeBetweenAdjacentSelectedFaces) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection split = qdr_test::edge_selection({
		make_edge(fixture.bottom_left, fixture.bottom_right),
		make_edge(fixture.top_right, fixture.top_left),
	});
	EXPECT_TRUE(connect_selected_edges(fixture.document, split).changed);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	Selection selection;
        selection.mode = SelectionMode::Face;
        for (const Face& face : fixture.document.faces) {
		selection.faces.push_back(face.id);
	}

	const OperationResult result = combine_selected_faces(fixture.document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.faces.size() == 1U);
	EXPECT_TRUE(fixture.document.faces.front().vertices.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationCombineFacesTests, CombineFacesRestoresAnInsetFaceIslandWithoutDependingOnDissolveOrder) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection inset_selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	EXPECT_TRUE(inset_selected_faces(fixture.document, inset_selection, 0.2F).changed);
	EXPECT_TRUE(fixture.document.faces.size() == 5U);

	Selection selection = qdr_test::all_face_selection(fixture.document);
	const OperationResult result = combine_selected_faces(fixture.document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.faces.size() == 1U);
	EXPECT_TRUE(fixture.document.faces.front().vertices.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

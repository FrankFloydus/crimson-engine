////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationCollapseFacesTests, CollapseFacesTurnsSelectedFaceAreaIntoOneSelectedVertex) {
	Document document = make_cube({});
	Selection selection = qdr_test::face_selection({ document.faces.front().id }, document.faces.front().id);
	const std::size_t vertex_count_before = document.vertices.size();

	const OperationResult result = collapse_selected_faces(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.vertices.size() < vertex_count_before);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 1U);
	EXPECT_TRUE(find_vertex(document, selection.vertices.front()) != nullptr);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

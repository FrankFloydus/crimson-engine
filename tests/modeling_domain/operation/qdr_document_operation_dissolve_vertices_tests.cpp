////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationDissolveVerticesTests, DissolveVerticesRemovesRedundantInlineVerticesAndSelectsNeighbors) {
	Document document;
	const ElementId left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId middle = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top = add_vertex(document, { 0.0F, 1.0F, 0.0F });
        EXPECT_TRUE(add_face(document,
                             std::array<ElementId, 4>{left, middle, right, top},
                             5) != kInvalidElementId);
        Selection selection = qdr_test::vertex_selection({ middle });

	const OperationResult result = dissolve_selected_vertices(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(find_vertex(document, middle) == nullptr);
	EXPECT_TRUE(document.faces.size() == 1U);
	EXPECT_TRUE(document.faces.front().vertices.size() == 3U);
	EXPECT_TRUE(selection_contains(selection, left));
	EXPECT_TRUE(selection_contains(selection, right));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationDissolveVerticesTests, DissolveVerticesRejectsTrueFaceCornersWithoutMutation) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::size_t vertex_count_before = fixture.document.vertices.size();
	const std::size_t face_count_before = fixture.document.faces.size();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left });

	const OperationResult result = dissolve_selected_vertices(fixture.document, selection);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before);
	EXPECT_TRUE(fixture.document.faces.size() == face_count_before);
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationSplitEdgesTests, SplitEdgesSeparatesAdjacentFacesWithoutDeletingEitherSide) {
	Document document = make_cube({});
	const Edge source_edge = document_edges(document).front();
	Selection selection = qdr_test::edge_selection({ source_edge }, source_edge);
	const std::size_t vertex_count_before = document.vertices.size();
	const std::size_t face_count_before = document.faces.size();

	const OperationResult result = split_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(document.faces.size() == face_count_before);
	EXPECT_TRUE(selection.mode == SelectionMode::edge);
	EXPECT_TRUE(selection.edges.size() == 2U);
	for (const Edge& edge : selection.edges) {
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, edge) == 1);
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

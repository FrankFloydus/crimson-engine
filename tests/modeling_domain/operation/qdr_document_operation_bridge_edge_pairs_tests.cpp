////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationBridgeEdgePairsTests, BridgeEdgePairsCreatesFacesBetweenExplicitEdgePairsOneExplicitPairBridgesOppositeOpenCubeEdges) {
	{
		Document document = make_cube({});
		const Face removed_face = document.faces.front();
		document.faces.erase(document.faces.begin());
		Selection selection;
		const Edge first = make_edge(removed_face.vertices[0], removed_face.vertices[1]);
		const Edge second = make_edge(removed_face.vertices[2], removed_face.vertices[3]);

		const OperationResult result = bridge_edge_pairs(document, selection, { { first, second } }, 2);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_valid_polygon_document_after_success(document, result);
                EXPECT_TRUE(selection.mode == SelectionMode::Face);
                EXPECT_TRUE(selection.faces.size() == 2U);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, first) == 2);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, second) == 2);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	}

	

	
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, BridgeEdgePairsCreatesFacesBetweenExplicitEdgePairsOneExplicitPairBridgesNonBoundaryCubeEdges) {
	

	{
		Document document = make_cube({});
		const std::size_t face_count_before = document.faces.size();
		Selection selection;
		const Edge first = make_edge(document.faces.front().vertices[0], document.faces.front().vertices[1]);
		const Edge second = make_edge(document.faces.front().vertices[2], document.faces.front().vertices[3]);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, first) == 2);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, second) == 2);

		const OperationResult result = bridge_edge_pairs(document, selection, { { first, second } }, 1);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_polygon_document_validation_issue_after_success(
			document,
			result,
			PolygonDocumentDiagnosticCode::NonmanifoldEdge);
		EXPECT_TRUE(document.faces.size() == face_count_before + 1U);
                EXPECT_TRUE(selection.mode == SelectionMode::Face);
                EXPECT_TRUE(selection.faces.size() == 1U);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, first) == 3);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, second) == 3);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	}

	
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, BridgeEdgePairsCreatesFacesBetweenExplicitEdgePairsEmptyPairListFailsWithoutMutating) {
	

	

	{
		Document document = make_cube({});
		Selection selection;

		const OperationResult empty_result = bridge_edge_pairs(document, selection, {}, 1);
		EXPECT_FALSE(empty_result.changed);
		EXPECT_TRUE(empty_result.message == "Bridge needs one or more selected edge pairs.");
	}
}

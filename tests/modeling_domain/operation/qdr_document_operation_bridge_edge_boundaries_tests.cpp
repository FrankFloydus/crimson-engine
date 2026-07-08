////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationBridgeEdgeBoundariesTests, BridgeEdgeBoundariesCreatesABridgeBetweenMatchingOpenChainsClosedMatchingBoundariesCreateASteppedShellBetweenFaceIslands) {
	{
		Document document = qdr_test::make_parallel_quad_document();
		EXPECT_TRUE(document.faces.size() == 2U);
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		const std::vector<ElementId> second_vertices = document.faces[1].vertices;
		Selection selection;

		const OperationResult result = bridge_edge_boundaries(document, selection, first_vertices, second_vertices, true, 2);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_valid_polygon_document_after_success(document, result);
                EXPECT_TRUE(selection.mode == SelectionMode::Face);
                EXPECT_TRUE(selection.faces.size() == 8U);
		EXPECT_TRUE(document.faces.size() == 10U);
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	}

	
}

TEST(QdrDocumentOperationBridgeEdgeBoundariesTests, BridgeEdgeBoundariesKeepsSharedEdgesOppositeForNormalAndInterpolatedBridge) {
	for (const int steps : {1, 3}) {
		Document document = qdr_test::make_parallel_quad_document();
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		const std::vector<ElementId> second_vertices = document.faces[1].vertices;
		Selection selection;

		const OperationResult result = bridge_edge_boundaries(
			document, selection, first_vertices, second_vertices, true, steps);

		ASSERT_TRUE(result.changed) << result.message;
		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
		EXPECT_EQ(selection.mode, SelectionMode::Face);
		EXPECT_EQ(selection.faces.size(), 4U * static_cast<std::size_t>(steps));
	}
}

TEST(QdrDocumentOperationBridgeEdgeBoundariesTests, BridgeEdgeBoundariesCreatesABridgeBetweenMatchingOpenChainsMismatchedOrTooShortBoundariesFailWithoutMutating) {
	

	{
		Document document = qdr_test::make_parallel_quad_document();
		const std::size_t face_count_before = document.faces.size();
		const std::vector<ElementId> first_vertices = document.faces[0].vertices;
		std::vector<ElementId> second_vertices = document.faces[1].vertices;
		second_vertices.pop_back();
		Selection selection;

		const OperationResult mismatch_result = bridge_edge_boundaries(document, selection, first_vertices, second_vertices, true, 1);
		EXPECT_FALSE(mismatch_result.changed);
		EXPECT_TRUE(mismatch_result.message == "Bridge needs matching boundary vertex counts.");
		EXPECT_TRUE(document.faces.size() == face_count_before);

		const std::vector<ElementId> short_vertices { first_vertices.front() };
		const OperationResult short_result = bridge_edge_boundaries(document, selection, short_vertices, short_vertices, false, 1);
		EXPECT_FALSE(short_result.changed);
		EXPECT_TRUE(short_result.message == "Bridge needs two valid boundary edge chains.");
	}
}

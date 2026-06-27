////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationConnectEdgesTests, ConnectEdgesCutsBetweenOppositeQuadEdgeMidpoints) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::edge_selection({
		make_edge(fixture.bottom_left, fixture.bottom_right),
		make_edge(fixture.top_right, fixture.top_left),
	});
	const std::size_t vertex_count_before = fixture.document.vertices.size();

	const OperationResult result = connect_selected_edges(fixture.document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	EXPECT_TRUE(selection.mode == SelectionMode::edge);
	EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(qdr_test::edge_positions_match_unordered(fixture.document, selection.edges.front(), { 0.0F, -1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }));
	EXPECT_TRUE(qdr_test::edge_incidence_count(fixture.document, selection.edges.front()) == 2);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationConnectEdgesTests, ConnectEdgesSplitsSharedCubeEdgesWithoutOpeningTheHull) {
	Document document = make_cube({});
	const Face face = document.faces.front();
	Selection selection = qdr_test::edge_selection({
		make_edge(face.vertices[0], face.vertices[1]),
		make_edge(face.vertices[2], face.vertices[3]),
	});
	const std::size_t vertex_count_before = document.vertices.size();

	const OperationResult result = connect_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(document.faces.size() == 7U);
	EXPECT_TRUE(selection.mode == SelectionMode::edge);
	EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

TEST(QdrDocumentOperationConnectEdgesTests, ConnectEdgesCutsAnUnorderedWallStripIntoACenterPath) {
	Document document;
	std::array<ElementId, 12> vertices {};
	for (int x = 0; x <= 5; ++x) {
		vertices[static_cast<std::size_t>(x)] = add_vertex(document, { static_cast<float>(x), 0.0F, 0.0F });
		vertices[static_cast<std::size_t>(x + 6)] = add_vertex(document, { static_cast<float>(x), 1.0F, 0.0F });
	}
	for (int x = 0; x < 5; ++x) {
		EXPECT_TRUE(add_face(document, std::array<ElementId, 4> {
			vertices[static_cast<std::size_t>(x)],
			vertices[static_cast<std::size_t>(x + 1)],
			vertices[static_cast<std::size_t>(x + 7)],
			vertices[static_cast<std::size_t>(x + 6)],
		}) != invalid_element_id);
	}
	Selection selection = qdr_test::edge_selection({
		make_edge(vertices[3], vertices[9]),
		make_edge(vertices[1], vertices[7]),
		make_edge(vertices[4], vertices[10]),
		make_edge(vertices[2], vertices[8]),
	});

	const OperationResult result = connect_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.vertices.size() == 16U);
	EXPECT_TRUE(document.faces.size() == 8U);
	EXPECT_TRUE(selection.mode == SelectionMode::edge);
	EXPECT_TRUE(selection.edges.size() == 3U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

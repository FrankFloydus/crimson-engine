////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

using namespace quader_poly;

TEST(QdrDocumentOperationConnectVerticesTests, ConnectVerticesSplitsASharedQuadFaceAndSelectsTheNewEdge) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const FaceTextureTransform texture{{2.0F, 0.5F}, {0.25F, -0.75F}, 15.0F};
	fixture.document.faces.front().texture_transform = texture;
	fixture.document.faces.front().uvs = {
		{0.0F, 1.0F}, {2.0F, 0.0F}, {3.0F, 4.0F}, {-1.0F, 2.0F}};
	const Face source = fixture.document.faces.front();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left, fixture.top_right });

	const OperationResult result = connect_selected_vertices(fixture.document, selection);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
		ASSERT_EQ(face.uvs.size(), face.vertices.size());
		for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
			const auto source_corner = std::ranges::find(source.vertices, face.vertices[corner]);
			ASSERT_NE(source_corner, source.vertices.end());
			const auto expected = source.uvs[static_cast<std::size_t>(
				std::distance(source.vertices.begin(), source_corner))];
			EXPECT_EQ(face.uvs[corner].x, expected.x);
			EXPECT_EQ(face.uvs[corner].y, expected.y);
		}
	}
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(selection_contains(selection, make_edge(fixture.bottom_left, fixture.top_right)));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationConnectVerticesTests, ConnectVerticesKeepsCubeCutsManifoldForDiagonalAndMultiVertexSelectionsTwoNonAdjacentCubeFaceVerticesCreateOneClosedDiagonal) {
	{
		Document document = make_cube({});
		const Face face = document.faces.front();
		Selection selection = qdr_test::vertex_selection({ face.vertices[0], face.vertices[2] });

		const OperationResult result = connect_selected_vertices(document, selection);

		EXPECT_TRUE(result.changed);
		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(document.faces.size() == 7U);
		EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(face.vertices[0], face.vertices[2])));
                EXPECT_TRUE(selection.mode == SelectionMode::Edge);
                EXPECT_TRUE(selection.edges.size() == 1U);
		EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	}

	
}

TEST(QdrDocumentOperationConnectVerticesTests, ConnectVerticesKeepsCubeCutsManifoldForDiagonalAndMultiVertexSelectionsThreeSelectedCubeFaceVerticesCreateTheClosingSelectedVertexEdgeSequence) {
	

	{
		Document document = make_cube({});
		const Face face = document.faces.front();
		Selection selection = qdr_test::vertex_selection({
			face.vertices[0],
			face.vertices[1],
			face.vertices[2],
		});

		const OperationResult result = connect_selected_vertices(document, selection);

		EXPECT_TRUE(result.changed);
		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(document.faces.size() == 7U);
		EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(face.vertices[2], face.vertices[0])));
                EXPECT_TRUE(selection.mode == SelectionMode::Edge);
                EXPECT_TRUE(selection.edges.size() == 3U);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	}
}

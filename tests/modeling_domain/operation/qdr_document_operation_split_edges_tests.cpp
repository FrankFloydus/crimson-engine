////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationSplitEdgesTests, SplitEdgesSeparatesAdjacentFacesWithoutDeletingEitherSide) {
	Document document = make_cube({});
	for (Face& face : document.faces) {
		const float island = static_cast<float>(face.id) * 10.0F;
		face.uvs = {{island, 0.0F}, {island + 2.0F, 0.0F},
		            {island + 3.0F, 1.0F}, {island, 4.0F}};
	}
	const Document source = document;
	const Edge source_edge = document_edges(document).front();
	Selection selection = qdr_test::edge_selection({ source_edge }, source_edge);
	const std::size_t vertex_count_before = document.vertices.size();
	const std::size_t face_count_before = document.faces.size();

	const OperationResult result = split_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(result.created.vertices.size() == 2U);
	EXPECT_TRUE(result.created.edges.size() == 1U);
	EXPECT_TRUE(result.created.faces.empty());
	EXPECT_TRUE(result.deleted.empty());
	EXPECT_TRUE(result.affected.vertices.empty());
	EXPECT_TRUE(result.affected.edges.size() == 1U);
	EXPECT_TRUE(result.affected.edges.front() == source_edge);
	EXPECT_TRUE(result.affected.faces.size() == 1U);
	EXPECT_TRUE(result.created.edges.front() != source_edge);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(document.faces.size() == face_count_before);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 2U);
	for (const Edge& edge : selection.edges) {
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, edge) == 1);
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	for (const Face& face : document.faces) {
		const Face* before = find_face(source, face.id);
		ASSERT_NE(before, nullptr);
		ASSERT_EQ(face.uvs.size(), before->uvs.size());
		ASSERT_EQ(face.vertices.size(), before->vertices.size());
		for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
			EXPECT_EQ(face.uvs[corner].x, before->uvs[corner].x);
			EXPECT_EQ(face.uvs[corner].y, before->uvs[corner].y);
			EXPECT_TRUE(qdr_test::vec_nearly_equal(
				find_vertex(document, face.vertices[corner])->position,
				find_vertex(source, before->vertices[corner])->position));
		}
	}
}

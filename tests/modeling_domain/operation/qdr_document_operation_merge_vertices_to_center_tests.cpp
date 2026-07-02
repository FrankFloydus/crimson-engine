////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

#include <algorithm>
#include <vector>

using namespace quader_poly;

TEST(QdrDocumentOperationMergeVerticesToCenterTests, MergeVerticesToCenterPlacesTheSurvivorAtTheSelectedCentroid) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right });

	const OperationResult result = merge_selected_vertices_to_center(fixture.document, selection);

	EXPECT_TRUE(result.changed);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 1U);
	const Vertex* survivor = find_vertex(fixture.document, selection.vertices.front());
	EXPECT_TRUE(survivor != nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, { 0.0F, -1.0F, 0.0F }));
	EXPECT_TRUE(find_vertex(fixture.document, fixture.bottom_right) == nullptr);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationMergeVerticesToCenterTests, MergeVerticesToCenterKeepsASimpleCubeHullClosed) {
	Document document = make_cube({});
	const Edge edge = document_edges(document).front();
	const Vertex* first = find_vertex(document, edge.a);
	const Vertex* second = find_vertex(document, edge.b);
	EXPECT_TRUE(first != nullptr);
	EXPECT_TRUE(second != nullptr);
	const quader::QVec3 expected_center = (first->position + second->position) / 2.0F;
	const std::size_t vertex_count_before = document.vertices.size();
	Selection selection = qdr_test::vertex_selection({ edge.a, edge.b }, edge.a);

	const OperationResult result = merge_selected_vertices_to_center(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before - 1U);
	const Vertex* survivor = find_vertex(document, edge.a);
	EXPECT_TRUE(survivor != nullptr);
	EXPECT_TRUE(find_vertex(document, edge.b) == nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, expected_center));
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.vertices.size() == 1U);
	EXPECT_TRUE(selection.active_vertex == edge.a);
	EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

TEST(QdrDocumentOperationMergeVerticesToCenterTests, MergeVerticesToCenterFansFoldedCubeSidesAroundOppositeTopCorners) {
	Document document = make_cube({});
	const ElementId bottom_front_left = 1;
	const ElementId bottom_front_right = 2;
	const ElementId top_front_right = 3;
	const ElementId top_front_left = 4;
	const ElementId bottom_back_left = 5;
	const ElementId top_back_left = 6;
	const ElementId top_back_right = 7;
	const ElementId bottom_back_right = 8;

	Selection selection = qdr_test::vertex_selection({ top_front_left, top_back_right }, top_back_right);

	const OperationResult result = merge_selected_vertices_to_center(document, selection);

	const auto has_face = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return std::any_of(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return face.vertices.size() == expected.size() && qdr_test::face_contains_all_vertices(face, expected);
		});
	};
	const auto has_cyclic_face = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return std::any_of(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return qdr_test::face_vertices_match_cyclic(face, expected);
		});
	};
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(find_vertex(document, top_front_left) == nullptr);
	const Vertex* survivor = find_vertex(document, top_back_right);
	EXPECT_TRUE(survivor != nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, { 0.0F, 1.0F, 0.0F }));
	EXPECT_FALSE(has_face({ bottom_back_left, bottom_front_left, bottom_front_right, top_front_right, top_back_right, top_back_left }));
	EXPECT_TRUE(has_cyclic_face({ bottom_front_left, bottom_front_right, top_front_right, top_back_right }));
	EXPECT_TRUE(has_cyclic_face({ bottom_back_left, bottom_front_left, top_back_right, top_back_left }));
	EXPECT_TRUE(has_cyclic_face({ bottom_back_right, top_back_right, top_front_right, bottom_front_right }));
	EXPECT_TRUE(has_cyclic_face({ bottom_back_left, top_back_left, top_back_right, bottom_back_right }));
	EXPECT_FALSE(has_face({ top_back_left, top_front_right, top_back_right }));
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(bottom_front_left, top_back_right)) == 2);
	EXPECT_TRUE(document.faces.size() == 5U);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.vertices.size() == 1U);
	EXPECT_TRUE(selection.active_vertex == top_back_right);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	const CompiledMesh mesh = compile_document(document);
	EXPECT_TRUE(mesh.primitives.size() == document.faces.size());
	EXPECT_TRUE(std::all_of(mesh.primitives.begin(), mesh.primitives.end(), [](const CompiledPrimitive& primitive) {
		return primitive.index_count == 6U;
	}));
}

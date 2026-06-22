////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

#include <algorithm>
#include <vector>

using namespace quader_poly;

TEST_CASE("merge vertices to center places the survivor at the selected centroid", "[operation][merge_vertices_to_center]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right });

	const OperationResult result = merge_selected_vertices_to_center(fixture.document, selection);

	REQUIRE(result.changed);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 1U);
	const Vertex* survivor = find_vertex(fixture.document, selection.vertices.front());
	REQUIRE(survivor != nullptr);
	REQUIRE(qdr_test::vec_nearly_equal(survivor->position, { 0.0F, -1.0F, 0.0F }));
	REQUIRE(find_vertex(fixture.document, fixture.bottom_right) == nullptr);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("merge vertices to center keeps a simple cube hull closed", "[operation][merge_vertices_to_center]")
{
	Document document = make_cube({});
	const Edge edge = document_edges(document).front();
	const Vertex* first = find_vertex(document, edge.a);
	const Vertex* second = find_vertex(document, edge.b);
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	const Vec3 expected_center = (first->position + second->position) / 2.0F;
	const std::size_t vertex_count_before = document.vertices.size();
	Selection selection = qdr_test::vertex_selection({ edge.a, edge.b }, edge.a);

	const OperationResult result = merge_selected_vertices_to_center(document, selection);

	REQUIRE(result.changed);
	REQUIRE(document.vertices.size() == vertex_count_before - 1U);
	const Vertex* survivor = find_vertex(document, edge.a);
	REQUIRE(survivor != nullptr);
	REQUIRE(find_vertex(document, edge.b) == nullptr);
	REQUIRE(qdr_test::vec_nearly_equal(survivor->position, expected_center));
	REQUIRE(selection.has_active);
	REQUIRE(selection.vertices.size() == 1U);
	REQUIRE(selection.active_vertex == edge.a);
	REQUIRE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(qdr_test::every_edge_is_closed(document));
}

TEST_CASE("merge vertices to center fans folded cube sides around opposite top corners", "[operation][merge_vertices_to_center]")
{
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
	REQUIRE(result.changed);
	REQUIRE(find_vertex(document, top_front_left) == nullptr);
	const Vertex* survivor = find_vertex(document, top_back_right);
	REQUIRE(survivor != nullptr);
	REQUIRE(qdr_test::vec_nearly_equal(survivor->position, { 0.0F, 1.0F, 0.0F }));
	REQUIRE_FALSE(has_face({ bottom_back_left, bottom_front_left, bottom_front_right, top_front_right, top_back_right, top_back_left }));
	REQUIRE(has_cyclic_face({ bottom_front_left, bottom_front_right, top_front_right, top_back_right }));
	REQUIRE(has_cyclic_face({ bottom_back_left, bottom_front_left, top_back_right, top_back_left }));
	REQUIRE(has_cyclic_face({ bottom_back_right, top_back_right, top_front_right, bottom_front_right }));
	REQUIRE(has_cyclic_face({ bottom_back_left, top_back_left, top_back_right, bottom_back_right }));
	REQUIRE_FALSE(has_face({ top_back_left, top_front_right, top_back_right }));
	REQUIRE(qdr_test::edge_incidence_count(document, make_edge(bottom_front_left, top_back_right)) == 2);
	REQUIRE(document.faces.size() == 5U);
	REQUIRE(selection.has_active);
	REQUIRE(selection.vertices.size() == 1U);
	REQUIRE(selection.active_vertex == top_back_right);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(qdr_test::every_edge_is_closed(document));
	const CompiledMesh mesh = compile_document(document);
	REQUIRE(mesh.primitives.size() == document.faces.size());
	REQUIRE(std::all_of(mesh.primitives.begin(), mesh.primitives.end(), [](const CompiledPrimitive& primitive) {
		return primitive.index_count == 6U;
	}));
}

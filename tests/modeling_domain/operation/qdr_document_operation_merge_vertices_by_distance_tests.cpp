////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

#include <algorithm>

using namespace quader_poly;

namespace {

/**
 * Stores the Two Triangle Fixture data contract used by the automated test suite.
 */
struct TwoTriangleFixture {
	Document document;
	ElementId a = invalid_element_id;
	ElementId b = invalid_element_id;
	ElementId c = invalid_element_id;
	ElementId d = invalid_element_id;
	ElementId e = invalid_element_id;
	ElementId f = invalid_element_id;
};

TwoTriangleFixture make_two_triangle_fixture(float duplicate_offset = 0.1F)
{
	TwoTriangleFixture fixture;
	fixture.a = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	fixture.b = add_vertex(fixture.document, { 1.0F, 0.0F, 0.0F });
	fixture.c = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	fixture.d = add_vertex(fixture.document, { 0.0F, 1.0F + duplicate_offset, 0.0F });
	fixture.e = add_vertex(fixture.document, { -1.0F, 1.0F, 0.0F });
	fixture.f = add_vertex(fixture.document, { -1.0F, 0.0F, 0.0F });
	EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 3> { fixture.a, fixture.b, fixture.c }) != invalid_element_id);
	EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 3> { fixture.d, fixture.e, fixture.f }) != invalid_element_id);
	return fixture;
}

Document make_touching_cube_pair_document(std::vector<ElementId>& first_interface, std::vector<ElementId>& second_interface)
{
	Document first = make_cube({});
	Document second = make_cube({});
	for (Vertex& vertex : first.vertices) {
		if (qdr_test::nearly_equal(vertex.position.x, 1.0F)) {
			first_interface.push_back(vertex.id);
		}
	}
	for (Vertex& vertex : second.vertices) {
		vertex.id += 1000U;
		vertex.position.x += 2.0F;
		if (qdr_test::nearly_equal(vertex.position.x, 1.0F)) {
			second_interface.push_back(vertex.id);
		}
	}
	for (Face& face : second.faces) {
		face.id += 1000U;
		for (ElementId& vertex_id : face.vertices) {
			vertex_id += 1000U;
		}
	}
	first.vertices.insert(first.vertices.end(), second.vertices.begin(), second.vertices.end());
	first.faces.insert(first.faces.end(), second.faces.begin(), second.faces.end());
	first.next_vertex_id = 2000U;
	first.next_face_id = 2000U;
	return first;
}

std::size_t face_count_on_plane_x(const Document& document, float x)
{
	return static_cast<std::size_t>(std::count_if(document.faces.begin(), document.faces.end(), [&document, x](const Face& face) {
		return std::all_of(face.vertices.begin(), face.vertices.end(), [&document, x](ElementId vertex_id) {
			const Vertex* vertex = find_vertex(document, vertex_id);
			return vertex != nullptr && qdr_test::nearly_equal(vertex->position.x, x);
		});
	}));
}

} // namespace

TEST(QdrDocumentOperationMergeVerticesByDistanceTests, MergeVerticesByDistanceWeldsSelectedVerticesInclusivelyToTheCentroid) {
	TwoTriangleFixture fixture = make_two_triangle_fixture();
	Selection selection = qdr_test::vertex_selection({ fixture.c, fixture.d });

	const OperationResult result = merge_selected_vertices_by_distance(fixture.document, selection, 0.1F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == 5U);
	const Vertex* survivor = find_vertex(fixture.document, fixture.c);
	EXPECT_TRUE(survivor != nullptr);
	EXPECT_TRUE(find_vertex(fixture.document, fixture.d) == nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, { 0.0F, 1.05F, 0.0F }));
	EXPECT_TRUE(selection.mode == SelectionMode::vertex);
	EXPECT_TRUE(selection.vertices.size() == 1U);
	EXPECT_TRUE(selection.vertices.front() == fixture.c);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_vertex == fixture.c);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests, MergeVerticesByDistanceKeepsSelectedVerticesWhenToleranceFindsNoCluster) {
	TwoTriangleFixture fixture = make_two_triangle_fixture(0.25F);
	Selection selection = qdr_test::vertex_selection({ fixture.c, fixture.d });
	const std::size_t vertex_count_before = fixture.document.vertices.size();

	const OperationResult result = merge_selected_vertices_by_distance(fixture.document, selection, 0.1F);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before);
	EXPECT_TRUE(selection.vertices.size() == 2U);
	EXPECT_TRUE(std::find(selection.vertices.begin(), selection.vertices.end(), fixture.c) != selection.vertices.end());
	EXPECT_TRUE(std::find(selection.vertices.begin(), selection.vertices.end(), fixture.d) != selection.vertices.end());
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests, MergeVerticesByDistancePicksTheMostCentralSurvivorForLargerClusters) {
	Document document;
	const ElementId a = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId b = add_vertex(document, { 0.05F, 0.0F, 0.0F });
	const ElementId c = add_vertex(document, { 0.1F, 0.0F, 0.0F });
	const ElementId a1 = add_vertex(document, { -1.5F, 1.0F, 0.0F });
	const ElementId a2 = add_vertex(document, { -0.5F, 1.0F, 0.0F });
	const ElementId b1 = add_vertex(document, { -0.5F, -1.0F, 0.0F });
	const ElementId b2 = add_vertex(document, { 0.5F, -1.0F, 0.0F });
	const ElementId c1 = add_vertex(document, { 0.5F, 1.0F, 0.0F });
	const ElementId c2 = add_vertex(document, { 1.5F, 1.0F, 0.0F });
	EXPECT_TRUE(add_face(document, std::array<ElementId, 3> { a, a1, a2 }) != invalid_element_id);
	EXPECT_TRUE(add_face(document, std::array<ElementId, 3> { b, b1, b2 }) != invalid_element_id);
	EXPECT_TRUE(add_face(document, std::array<ElementId, 3> { c, c1, c2 }) != invalid_element_id);
	Selection selection = qdr_test::vertex_selection({ a, b, c });

	const OperationResult result = merge_selected_vertices_by_distance(document, selection, 1.1F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(find_vertex(document, a) == nullptr);
	EXPECT_TRUE(find_vertex(document, c) == nullptr);
	const Vertex* survivor = find_vertex(document, b);
	EXPECT_TRUE(survivor != nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, { -0.28333333F, 0.0F, 0.0F }));
	EXPECT_TRUE(selection.vertices.size() == 1U);
	EXPECT_TRUE(selection.vertices.front() == b);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests, MergeVerticesByDistanceCollapsesDuplicateFacesCreatedByWeldedCubeSides) {
	std::vector<ElementId> first_interface;
	std::vector<ElementId> second_interface;
	Document document = make_touching_cube_pair_document(first_interface, second_interface);
	EXPECT_TRUE(first_interface.size() == 4U);
	EXPECT_TRUE(second_interface.size() == 4U);
	EXPECT_TRUE(face_count_on_plane_x(document, 1.0F) == 2U);
	const std::size_t vertex_count_before = document.vertices.size();

	Selection selection;
	selection.mode = SelectionMode::vertex;
	selection.vertices.insert(selection.vertices.end(), first_interface.begin(), first_interface.end());
	selection.vertices.insert(selection.vertices.end(), second_interface.begin(), second_interface.end());

	const OperationResult result = merge_selected_vertices_by_distance(document, selection, 0.0F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before - 4U);
	EXPECT_TRUE(document.faces.size() == 11U);
	EXPECT_TRUE(face_count_on_plane_x(document, 1.0F) == 1U);
	EXPECT_TRUE(selection.mode == SelectionMode::vertex);
	EXPECT_TRUE(selection.vertices.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationMergeVerticesByDistanceTests, MergeVerticesByDistanceAllowsNonManifoldEdgeClusters) {
	Document document;
	const ElementId a1 = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId b1 = add_vertex(document, { 0.0F, 0.0F, 1.0F });
	const ElementId a2 = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId b2 = add_vertex(document, { 0.0F, 0.0F, 1.0F });
	const ElementId a3 = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId b3 = add_vertex(document, { 0.0F, 0.0F, 1.0F });
	const ElementId c1 = add_vertex(document, { 1.0F, 0.0F, 1.0F });
	const ElementId d1 = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId c2 = add_vertex(document, { 0.0F, 1.0F, 1.0F });
	const ElementId d2 = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId c3 = add_vertex(document, { -1.0F, 0.0F, 1.0F });
	const ElementId d3 = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	EXPECT_TRUE(add_face(document, std::array<ElementId, 4> { a1, b1, c1, d1 }) != invalid_element_id);
	EXPECT_TRUE(add_face(document, std::array<ElementId, 4> { a2, b2, c2, d2 }) != invalid_element_id);
	EXPECT_TRUE(add_face(document, std::array<ElementId, 4> { a3, b3, c3, d3 }) != invalid_element_id);
	Selection selection = qdr_test::vertex_selection({ a1, b1, a2, b2, a3, b3 });

	const OperationResult result = merge_selected_vertices_by_distance(document, selection, 0.001F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(document.vertices.size() == 8U);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(a1, b1)) == 3);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

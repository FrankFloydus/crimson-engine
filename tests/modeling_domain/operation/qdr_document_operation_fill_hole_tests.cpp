////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <array>

using namespace quader_poly;

namespace {

/**
 * Stores the Circular Boundary Fixture data contract used by the automated test suite.
 */
struct CircularBoundaryFixture {
	Document document;
	std::array<Edge, 8> inner_edges {};
};

CircularBoundaryFixture make_circular_boundary_fixture()
{
	CircularBoundaryFixture fixture;
	std::array<ElementId, 8> inner {};
	std::array<ElementId, 8> outer {};
	const std::array<quader::QVec3, 8> directions {
		quader::QVec3 { -0.70710677F, 0.0F, -0.70710677F },
		quader::QVec3 { 0.0F, 0.0F, -1.0F },
		quader::QVec3 { 0.70710677F, 0.0F, -0.70710677F },
		quader::QVec3 { 1.0F, 0.0F, 0.0F },
		quader::QVec3 { 0.70710677F, 0.0F, 0.70710677F },
		quader::QVec3 { 0.0F, 0.0F, 1.0F },
		quader::QVec3 { -0.70710677F, 0.0F, 0.70710677F },
		quader::QVec3 { -1.0F, 0.0F, 0.0F },
	};

	for (std::size_t index = 0; index < directions.size(); ++index) {
		inner[index] = add_vertex(fixture.document, directions[index] * 0.5F);
		outer[index] = add_vertex(fixture.document, directions[index] * 0.75F);
	}

	for (std::size_t index = 0; index < directions.size(); ++index) {
		const std::size_t next = (index + 1U) % directions.size();
                EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                           inner[index],
                                                           inner[next],
                                                           outer[next],
                                                           outer[index],
                                                       }) != kInvalidElementId);
                fixture.inner_edges[index] = make_edge(inner[index], inner[next]);
	}
	return fixture;
}

} // namespace

TEST(QdrDocumentOperationFillHoleTests, FillHoleCreatesOneFaceFromASelectedOpenEdgeLoop) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	const quader::QVec3 removed_face_normal = face_normal(document, removed_face);
	document.faces.erase(document.faces.begin());
	Selection selection;
        selection.mode = SelectionMode::Edge;
        for (std::size_t index = 0; index < removed_face.vertices.size(); ++index) {
		selection.edges.push_back(make_edge(removed_face.vertices[index], removed_face.vertices[(index + 1U) % removed_face.vertices.size()]));
	}

	const OperationResult result = fill_hole_from_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.faces.size() == 6U);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* fill_face = find_face(document, selection.faces.front());
	EXPECT_TRUE(fill_face != nullptr);
	EXPECT_TRUE(dot(face_normal(document, *fill_face), removed_face_normal) > 0.5F);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleRejectsASelectedNonBoundaryEdgeLoop) {
	Document document = make_cube({});
	Face& source_face = document.faces.front();
	source_face.material_slot = 5U;
	Selection selection;
        selection.mode = SelectionMode::Edge;
        for (std::size_t index = 0; index < source_face.vertices.size(); ++index) {
		selection.edges.push_back(make_edge(source_face.vertices[index], source_face.vertices[(index + 1U) % source_face.vertices.size()]));
	}
	const Edge first_edge = selection.edges.front();
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, first_edge) == 2);
	const std::size_t face_count_before = document.faces.size();

	const OperationResult result = fill_hole_from_selected_edges(document, selection);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(document.faces.size() == face_count_before);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, first_edge) == 2);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationFillHoleTests, FillHoleOrientsCapsFromAdjacentBoundaryWindingAfterCircularEdgeExtrusion) {
	CircularBoundaryFixture fixture = make_circular_boundary_fixture();
	Selection selection = qdr_test::edge_selection({
		fixture.inner_edges[0],
		fixture.inner_edges[1],
		fixture.inner_edges[2],
		fixture.inner_edges[3],
		fixture.inner_edges[4],
		fixture.inner_edges[5],
		fixture.inner_edges[6],
		fixture.inner_edges[7],
	}, fixture.inner_edges.back());

	EXPECT_TRUE(extrude_selected_elements(fixture.document, selection, quader::QVec3 { 0.0F, -0.5F, 0.0F }).changed);

	qdr_test::expect_valid_polygon_document(fixture.document, "successful topology operation");
	const Selection copied_boundary_selection = selection;

	const OperationResult result = fill_hole_from_selected_edges(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* fill_face = find_face(fixture.document, selection.faces.front());
	EXPECT_TRUE(fill_face != nullptr);
	EXPECT_TRUE(dot(face_normal(fixture.document, *fill_face), quader::QVec3 { 0.0F, 1.0F, 0.0F }) > 0.5F);
	EXPECT_TRUE(qdr_test::shared_edges_have_opposite_winding(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));

	Selection repeat_selection = copied_boundary_selection;
	const std::size_t face_count_after_fill = fixture.document.faces.size();
	const OperationResult repeat_result = fill_hole_from_selected_edges(fixture.document, repeat_selection);
	EXPECT_FALSE(repeat_result.changed);
	EXPECT_TRUE(fixture.document.faces.size() == face_count_after_fill);
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

namespace {

/**
 * Stores the Angled Gap Fixture data contract used by the automated test suite.
 */
struct AngledGapFixture {
	Document document;
	Edge left_edge;
	Edge right_edge;
};

/**
 * Stores the Crossed Gap Fixture data contract used by the automated test suite.
 */
struct CrossedGapFixture {
	Document document;
	Edge left_edge;
	Edge right_edge;
        ElementId left_front = kInvalidElementId;
        ElementId left_back = kInvalidElementId;
        ElementId right_front = kInvalidElementId;
        ElementId right_back = kInvalidElementId;
};

AngledGapFixture make_angled_gap_fixture()
{
	AngledGapFixture fixture;
	const ElementId left_front = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	const ElementId left_back = add_vertex(fixture.document, { 0.0F, 0.0F, 1.0F });
	const ElementId left_front_top = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	const ElementId left_back_top = add_vertex(fixture.document, { 0.0F, 1.0F, 1.0F });
	const ElementId right_front = add_vertex(fixture.document, { 1.0F, 0.0F, 0.0F });
	const ElementId right_back = add_vertex(fixture.document, { 1.5F, 0.0F, 0.5F });
	const ElementId right_front_top = add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	const ElementId right_back_top = add_vertex(fixture.document, { 1.5F, 1.0F, 0.5F });

	// Add the non-gap side first so document-order adjacent-face picking would
	// wind the new bridge downward across this angled gap.
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 left_front, left_back, left_back_top,
                                 left_front_top}) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 right_front, right_front_top, right_back_top,
                                 right_back}) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 left_front, left_front_top, left_back_top,
                                 left_back}) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document,
                             std::array<ElementId, 4>{
                                 right_front, right_back, right_back_top,
                                 right_front_top}) != kInvalidElementId);

        fixture.left_edge = make_edge(left_front, left_back);
	fixture.right_edge = make_edge(right_front, right_back);
	return fixture;
}

CrossedGapFixture make_crossed_gap_fixture()
{
	CrossedGapFixture fixture;
	fixture.left_front = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	fixture.left_back = add_vertex(fixture.document, { 0.0F, 0.0F, 2.0F });
	const ElementId left_front_top = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	const ElementId left_back_top = add_vertex(fixture.document, { 0.0F, 1.0F, 2.0F });
	fixture.right_front = add_vertex(fixture.document, { 4.0F, 0.0F, 0.0F });
	fixture.right_back = add_vertex(fixture.document, { 3.0F, 0.0F, 2.0F });
	const ElementId right_front_top = add_vertex(fixture.document, { 4.0F, 1.0F, 0.0F });
	const ElementId right_back_top = add_vertex(fixture.document, { 3.0F, 1.0F, 2.0F });
	const ElementId right_front_bottom = add_vertex(fixture.document, { 4.0F, -1.0F, 0.0F });
	const ElementId right_back_bottom = add_vertex(fixture.document, { 3.0F, -1.0F, 2.0F });

        EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                   fixture.left_back,
                                                   fixture.left_front,
                                                   left_front_top,
                                                   left_back_top,
                                               }) != kInvalidElementId);
        // Add this tied-scoring adjacent face first. A one-edge-at-a-time bridge
	// side pick would use it and cross the generated quad diagonally.
        EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                   fixture.right_back,
                                                   fixture.right_front,
                                                   right_front_bottom,
                                                   right_back_bottom,
                                               }) != kInvalidElementId);
        EXPECT_TRUE(add_face(fixture.document, std::array<ElementId, 4>{
                                                   fixture.right_front,
                                                   fixture.right_back,
                                                   right_back_top,
                                                   right_front_top,
                                               }) != kInvalidElementId);

        fixture.left_edge = make_edge(fixture.left_front, fixture.left_back);
	fixture.right_edge = make_edge(fixture.right_front, fixture.right_back);
	return fixture;
}

} // namespace

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesCreatesASingleQuadBetweenTwoOpenEdges) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	document.faces.erase(document.faces.begin());
	Selection selection = qdr_test::edge_selection({
		make_edge(removed_face.vertices[0], removed_face.vertices[1]),
		make_edge(removed_face.vertices[2], removed_face.vertices[3]),
	});

	const OperationResult result = bridge_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* bridged_face = find_face(document, selection.faces.front());
	EXPECT_TRUE(bridged_face != nullptr);
	EXPECT_TRUE(bridged_face->vertices.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesChoosesGapFacingAdjacentFacesForAngledNonBoundarySpans) {
	AngledGapFixture fixture = make_angled_gap_fixture();
	Selection selection = qdr_test::edge_selection({
		fixture.left_edge,
		fixture.right_edge,
	});

	const OperationResult result = bridge_selected_edges(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_polygon_document_validation_issue_after_success(
		fixture.document,
		result,
		PolygonDocumentDiagnosticCode::NonmanifoldEdge);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* bridged_face = find_face(fixture.document, selection.faces.front());
	EXPECT_TRUE(bridged_face != nullptr);
	EXPECT_TRUE(dot(face_normal(fixture.document, *bridged_face), quader::QVec3 { 0.0F, 1.0F, 0.0F }) > 0.5F);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesPairsEndpointsWithoutCrossingAngledGapFaces) {
	CrossedGapFixture fixture = make_crossed_gap_fixture();
	Selection selection = qdr_test::edge_selection({
		fixture.left_edge,
		fixture.right_edge,
	});

	const OperationResult result = bridge_selected_edges(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_polygon_document_validation_issue_after_success(
		fixture.document,
		result,
		PolygonDocumentDiagnosticCode::NonmanifoldEdge);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* bridged_face = find_face(fixture.document, selection.faces.front());
	EXPECT_TRUE(bridged_face != nullptr);
	EXPECT_TRUE(qdr_test::face_vertices_match_cyclic(*bridged_face, {
		fixture.left_front,
		fixture.left_back,
		fixture.right_back,
		fixture.right_front,
	}));
	EXPECT_TRUE(dot(face_normal(fixture.document, *bridged_face), quader::QVec3 { 0.0F, 1.0F, 0.0F }) > 0.5F);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationBridgeEdgesTests, BridgeEdgesSupportsSteppedSpansBetweenTwoOpenEdges) {
	Document document = make_cube({});
	const Face removed_face = document.faces.front();
	document.faces.erase(document.faces.begin());
	bool has_plane_z = false;
	float plane_z = 0.0F;
	for (ElementId vertex_id : removed_face.vertices) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		if (vertex == nullptr) {
			continue;
		}
		plane_z = has_plane_z ? std::max(plane_z, vertex->position.z) : vertex->position.z;
		has_plane_z = true;
	}
	Selection selection = qdr_test::edge_selection({
		make_edge(removed_face.vertices[0], removed_face.vertices[1]),
		make_edge(removed_face.vertices[2], removed_face.vertices[3]),
	});

	const OperationResult result = bridge_selected_edges(document, selection, 3);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 3U);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(removed_face.vertices[0], removed_face.vertices[1])) == 2);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(removed_face.vertices[2], removed_face.vertices[3])) == 2);
	EXPECT_TRUE(has_plane_z);
	EXPECT_TRUE(std::any_of(document.vertices.begin(), document.vertices.end(), [plane_z](const Vertex& vertex) {
		return vertex.position.z > plane_z + 0.001F;
	}));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationInsertEdgeLoopTests, InsertEdgeLoopSplitsAQuadThroughTheSelectedEdgeFactor) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(7);
	Face* face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	face->uvs = {
		{ 0.0F, 0.0F },
		{ 1.0F, 0.0F },
		{ 1.0F, 1.0F },
		{ 0.0F, 1.0F },
	};
	const Edge edge = make_edge(fixture.bottom_left, fixture.bottom_right);
	Selection selection = qdr_test::edge_selection({ edge }, edge);

	const OperationResult result = insert_edge_loop(fixture.document, selection, 0.25F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == 6U);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	EXPECT_TRUE(selection.mode == SelectionMode::edge);
	EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_kind == ElementKind::edge);
	EXPECT_TRUE(qdr_test::edge_positions_match_unordered(fixture.document, selection.edges.front(), { -0.5F, -1.0F, 0.0F }, { -0.5F, 1.0F, 0.0F }));
	for (const Face& split_face : fixture.document.faces) {
		EXPECT_TRUE(split_face.material_slot == 7U);
		EXPECT_TRUE(split_face.uvs.empty());
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, InsertEdgeLoopPreservesSideEdgeFactorDirectionAndClampsExtremesSideEdgeFactorsFollowTheSelectedSeedDirection) {
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		Selection selection = qdr_test::edge_selection({ make_edge(fixture.bottom_left, fixture.top_left) });

		const OperationResult result = insert_edge_loop(fixture.document, selection, 0.75F);

		EXPECT_TRUE(result.changed);
		EXPECT_TRUE(selection.edges.size() == 1U);
		EXPECT_TRUE(qdr_test::edge_positions_match_unordered(fixture.document, selection.edges.front(), { -1.0F, 0.5F, 0.0F }, { 1.0F, 0.5F, 0.0F }));
	}

	
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, InsertEdgeLoopPreservesSideEdgeFactorDirectionAndClampsExtremesOutOfRangeFactorsClampInsideTheSourceEdge) {
	

	{
		qdr_test::QuadFixture low = qdr_test::make_quad_document();
		Selection low_selection = qdr_test::edge_selection({ make_edge(low.bottom_left, low.bottom_right) });
		EXPECT_TRUE(insert_edge_loop(low.document, low_selection, -4.0F).changed);
		EXPECT_TRUE(qdr_test::edge_positions_match_unordered(low.document, low_selection.edges.front(), { -0.98F, -1.0F, 0.0F }, { -0.98F, 1.0F, 0.0F }));

		qdr_test::QuadFixture high = qdr_test::make_quad_document();
		Selection high_selection = qdr_test::edge_selection({ make_edge(high.bottom_left, high.bottom_right) });
		EXPECT_TRUE(insert_edge_loop(high.document, high_selection, 4.0F).changed);
		EXPECT_TRUE(qdr_test::edge_positions_match_unordered(high.document, high_selection.edges.front(), { 0.98F, -1.0F, 0.0F }, { 0.98F, 1.0F, 0.0F }));
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, InsertEdgeLoopTraversesAClosedCubeQuadLoop) {
	Document document = make_cube({});
	const Face face = document.faces.front();
	Selection selection = qdr_test::edge_selection({ make_edge(face.vertices[0], face.vertices[1]) });
	const std::size_t vertex_count_before = document.vertices.size();

	const OperationResult result = insert_edge_loop(document, selection, 0.5F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 4U);
	EXPECT_TRUE(document.faces.size() == 10U);
	EXPECT_TRUE(selection.mode == SelectionMode::edge);
	EXPECT_TRUE(selection.edges.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, InsertEdgeLoopAllowsUnrelatedNonManifoldTopology) {
	Document document;
	const ElementId shared_a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId shared_b = add_vertex(document, { 0.0F, 0.0F, 1.0F });
	const ElementId c1 = add_vertex(document, { 1.0F, 0.0F, 1.0F });
	const ElementId d1 = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId c2 = add_vertex(document, { 0.0F, 1.0F, 1.0F });
	const ElementId d2 = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId c3 = add_vertex(document, { -1.0F, 0.0F, 1.0F });
	const ElementId d3 = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	EXPECT_TRUE(add_face(document, std::array<ElementId, 4> { shared_a, shared_b, c1, d1 }) != invalid_element_id);
	EXPECT_TRUE(add_face(document, std::array<ElementId, 4> { shared_a, shared_b, c2, d2 }) != invalid_element_id);
	EXPECT_TRUE(add_face(document, std::array<ElementId, 4> { shared_a, shared_b, c3, d3 }) != invalid_element_id);

	const ElementId qa = add_vertex(document, { 3.0F, 0.0F, 0.0F });
	const ElementId qb = add_vertex(document, { 4.0F, 0.0F, 0.0F });
	const ElementId qc = add_vertex(document, { 4.0F, 1.0F, 0.0F });
	const ElementId qd = add_vertex(document, { 3.0F, 1.0F, 0.0F });
	EXPECT_TRUE(add_face(document, std::array<ElementId, 4> { qa, qb, qc, qd }) != invalid_element_id);
	Selection selection = qdr_test::edge_selection({ make_edge(qa, qb) });

	const OperationResult result = insert_edge_loop(document, selection, 0.5F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(shared_a, shared_b)) == 3);
	EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, EdgeLoopSelectionSurvivesConversionFromAPickedFace) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();

	PickResult face_pick;
	face_pick.hit = true;
	face_pick.kind = ElementKind::face;
	face_pick.face_id = fixture.face;
	Selection selection;
	select_only(selection, face_pick);

	EXPECT_TRUE(selection.mode == SelectionMode::face);
	EXPECT_TRUE(selection_contains_face(selection, fixture.face));
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_kind == ElementKind::face);
	EXPECT_TRUE(selection.active_face == fixture.face);

	convert_selection_mode(fixture.document, selection, SelectionMode::edge);

	EXPECT_TRUE(selection.mode == SelectionMode::edge);
	EXPECT_TRUE(selection.edges.size() == 4U);
	EXPECT_TRUE(selection_contains(selection, make_edge(fixture.bottom_left, fixture.bottom_right)));

	const std::optional<quader::QVec3> center = selection_center(fixture.document, selection);
	EXPECT_TRUE(center.has_value());
	EXPECT_NEAR(center->x, 0.0F, 0.0001);
	EXPECT_NEAR(center->y, 0.0F, 0.0001);
	EXPECT_NEAR(center->z, 0.0F, 0.0001);

	Selection loop_selection;
	const Edge seed = make_edge(fixture.bottom_left, fixture.bottom_right);
	select_edge_loop(fixture.document, loop_selection, seed);

	EXPECT_TRUE(loop_selection.mode == SelectionMode::edge);
	EXPECT_FALSE(loop_selection.edges.empty());
	EXPECT_TRUE(selection_contains(loop_selection, seed));
}

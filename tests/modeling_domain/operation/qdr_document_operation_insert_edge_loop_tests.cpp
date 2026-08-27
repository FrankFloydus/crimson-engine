////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

namespace {

/// Owns a fixture where a quad strip continues into a triangle or ngon.
struct NonQuadLoopPathFixture {
	Document document;
	Edge seed;
	Edge shared_edge;
	ElementId non_quad_face = kInvalidElementId;
};

NonQuadLoopPathFixture make_nonquad_loop_path_fixture(
	std::size_t non_quad_side_count) {
	NonQuadLoopPathFixture fixture;
	const ElementId bottom_left =
		add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
	const ElementId bottom_right =
		add_vertex(fixture.document, { 1.0F, 0.0F, 0.0F });
	const ElementId shared_right =
		add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	const ElementId shared_left =
		add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	EXPECT_NE(add_face(fixture.document,
	                   std::array<ElementId, 4>{ bottom_left, bottom_right,
	                                              shared_right, shared_left }),
	          kInvalidElementId);

	std::vector<ElementId> non_quad_vertices { shared_left, shared_right };
	if (non_quad_side_count == 3U) {
		non_quad_vertices.push_back(
			add_vertex(fixture.document, { 0.5F, 2.0F, 0.0F }));
	} else {
		EXPECT_EQ(non_quad_side_count, 5U);
		non_quad_vertices.push_back(
			add_vertex(fixture.document, { 1.25F, 1.75F, 0.0F }));
		non_quad_vertices.push_back(
			add_vertex(fixture.document, { 0.5F, 2.25F, 0.0F }));
		non_quad_vertices.push_back(
			add_vertex(fixture.document, { -0.25F, 1.75F, 0.0F }));
	}
	fixture.non_quad_face = add_face(fixture.document, non_quad_vertices);
	EXPECT_NE(fixture.non_quad_face, kInvalidElementId);

	fixture.seed = make_edge(bottom_left, bottom_right);
	fixture.shared_edge = make_edge(shared_left, shared_right);
	EXPECT_EQ(qdr_test::edge_incidence_count(fixture.document,
	                                         fixture.shared_edge),
	          2);
	qdr_test::expect_valid_polygon_document(
		fixture.document, "quad-to-non-quad edge-loop fixture");
	return fixture;
}

void expect_nonquad_path_preview_stops_after_quad(std::size_t non_quad_side_count) {
	const NonQuadLoopPathFixture fixture =
		make_nonquad_loop_path_fixture(non_quad_side_count);

	const OverlayData preview =
		build_edge_loop_preview(fixture.document, fixture.seed, 0.5F);

	ASSERT_EQ(preview.lines.size(), 1U);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(
		preview.lines.front().start, { 0.5F, 0.0F, 0.0F }));
	EXPECT_TRUE(qdr_test::vec_nearly_equal(
		preview.lines.front().end, { 0.5F, 1.0F, 0.0F }));
}

void expect_nonquad_path_commit_splits_quad(std::size_t non_quad_side_count) {
	NonQuadLoopPathFixture fixture =
		make_nonquad_loop_path_fixture(non_quad_side_count);
	const std::size_t vertex_count_before = fixture.document.vertices.size();
	const std::size_t face_count_before = fixture.document.faces.size();
	const Face* non_quad_face_before =
		find_face(fixture.document, fixture.non_quad_face);
	ASSERT_NE(non_quad_face_before, nullptr);
	const std::vector<ElementId> non_quad_vertices_before =
		non_quad_face_before->vertices;
	Selection selection =
		qdr_test::edge_selection({ fixture.seed }, fixture.seed);

	const OperationResult result =
		insert_edge_loop(fixture.document, selection, 0.5F);

	EXPECT_TRUE(result.changed);
	EXPECT_EQ(fixture.document.vertices.size(), vertex_count_before + 2U);
	EXPECT_EQ(fixture.document.faces.size(), face_count_before + 1U);
	const Face* non_quad_face_after =
		find_face(fixture.document, fixture.non_quad_face);
	ASSERT_NE(non_quad_face_after, nullptr);
	EXPECT_EQ(non_quad_face_after->vertices, non_quad_vertices_before);
	ASSERT_EQ(selection.edges.size(), 1U);
	EXPECT_TRUE(qdr_test::edge_positions_match_unordered(
		fixture.document,
		selection.edges.front(),
		{ 0.5F, 0.0F, 0.0F },
		{ 0.5F, 1.0F, 0.0F }));
}

} // namespace

TEST(QdrDocumentOperationInsertEdgeLoopTests, InsertEdgeLoopSplitsAQuadThroughTheSelectedEdgeFactor) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(7);
	Face* face = find_face(fixture.document, fixture.face);
	EXPECT_TRUE(face != nullptr);
	const FaceTextureTransform texture{{-2.0F, 0.5F}, {0.25F, 0.75F}, 45.0F};
	face->texture_transform = texture;
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

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.vertices.size() == 6U);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(selection.has_active);
        EXPECT_TRUE(selection.active_kind == ElementKind::Edge);
        EXPECT_TRUE(qdr_test::edge_positions_match_unordered(fixture.document, selection.edges.front(), { -0.5F, -1.0F, 0.0F }, { -0.5F, 1.0F, 0.0F }));
	for (const Face& split_face : fixture.document.faces) {
		EXPECT_TRUE(split_face.material_slot == 7U);
		EXPECT_EQ(split_face.texture_transform, texture);
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

		qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
		EXPECT_TRUE(selection.edges.size() == 1U);
		EXPECT_TRUE(qdr_test::edge_positions_match_unordered(fixture.document, selection.edges.front(), { -1.0F, 0.5F, 0.0F }, { 1.0F, 0.5F, 0.0F }));
	}

	
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, InsertEdgeLoopPreservesSideEdgeFactorDirectionAndClampsExtremesOutOfRangeFactorsClampInsideTheSourceEdge) {
	

	{
		qdr_test::QuadFixture low = qdr_test::make_quad_document();
		Selection low_selection = qdr_test::edge_selection({ make_edge(low.bottom_left, low.bottom_right) });
		EXPECT_TRUE(insert_edge_loop(low.document, low_selection, -4.0F).changed);
		qdr_test::expect_valid_polygon_document(low.document, "successful topology operation");
		EXPECT_TRUE(qdr_test::edge_positions_match_unordered(low.document, low_selection.edges.front(), { -0.98F, -1.0F, 0.0F }, { -0.98F, 1.0F, 0.0F }));

		qdr_test::QuadFixture high = qdr_test::make_quad_document();
		Selection high_selection = qdr_test::edge_selection({ make_edge(high.bottom_left, high.bottom_right) });
		EXPECT_TRUE(insert_edge_loop(high.document, high_selection, 4.0F).changed);
		qdr_test::expect_valid_polygon_document(high.document, "successful topology operation");
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

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 4U);
	EXPECT_TRUE(document.faces.size() == 10U);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopPreviewStopsAfterQuadAtTrianglePath) {
	expect_nonquad_path_preview_stops_after_quad(3U);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopCommitSplitsQuadAtTrianglePath) {
	expect_nonquad_path_commit_splits_quad(3U);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopPreviewStopsAfterQuadAtNgonPath) {
	expect_nonquad_path_preview_stops_after_quad(5U);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopCommitSplitsQuadAtNgonPath) {
	expect_nonquad_path_commit_splits_quad(5U);
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
        EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                           shared_a, shared_b, c1,
                                           d1}) != kInvalidElementId);
        EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                           shared_a, shared_b, c2,
                                           d2}) != kInvalidElementId);
        EXPECT_TRUE(add_face(document, std::array<ElementId, 4>{
                                           shared_a, shared_b, c3,
                                           d3}) != kInvalidElementId);

        const ElementId qa = add_vertex(document, { 3.0F, 0.0F, 0.0F });
	const ElementId qb = add_vertex(document, { 4.0F, 0.0F, 0.0F });
	const ElementId qc = add_vertex(document, { 4.0F, 1.0F, 0.0F });
	const ElementId qd = add_vertex(document, { 3.0F, 1.0F, 0.0F });
        EXPECT_TRUE(
            add_face(document, std::array<ElementId, 4>{qa, qb, qc, qd}) !=
            kInvalidElementId);
        Selection selection = qdr_test::edge_selection({ make_edge(qa, qb) });

	const OperationResult result = insert_edge_loop(document, selection, 0.5F);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_polygon_document_validation_issue_after_success(
		document,
		result,
		PolygonDocumentDiagnosticCode::NonmanifoldEdge);
	EXPECT_TRUE(qdr_test::edge_incidence_count(document, make_edge(shared_a, shared_b)) == 3);
	EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, EdgeLoopSelectionSurvivesConversionFromAPickedFace) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();

	PickResult face_pick;
	face_pick.hit = true;
        face_pick.kind = ElementKind::Face;
        face_pick.face_id = fixture.face;
	Selection selection;
	select_only(selection, face_pick);

        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection_contains_face(selection, fixture.face));
	EXPECT_TRUE(selection.has_active);
        EXPECT_TRUE(selection.active_kind == ElementKind::Face);
        EXPECT_TRUE(selection.active_face == fixture.face);

        convert_selection_mode(fixture.document, selection,
                               SelectionMode::Edge);

        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
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

        EXPECT_TRUE(loop_selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(loop_selection.edges.size() == 4U);
	EXPECT_TRUE(selection_contains(loop_selection, seed));
	EXPECT_TRUE(selection_contains(loop_selection, make_edge(fixture.bottom_right, fixture.top_right)));
	EXPECT_TRUE(selection_contains(loop_selection, make_edge(fixture.top_right, fixture.top_left)));
	EXPECT_TRUE(selection_contains(loop_selection, make_edge(fixture.top_left, fixture.bottom_left)));
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, EdgeLoopSelectionFollowsBoundaryFanThroughSharedEdges) {
	qdr_test::BoundaryFanLoopFixture fixture = qdr_test::make_boundary_fan_loop_document();

	Selection selection;
	select_edge_loop(fixture.document, selection, fixture.seed);

	EXPECT_TRUE(selection.mode == SelectionMode::Edge);
	ASSERT_TRUE(selection.edges.size() == fixture.expected_loop_edges.size());
	for (const Edge& edge : fixture.expected_loop_edges) {
		EXPECT_TRUE(selection_contains(selection, edge));
	}
	for (const Edge& edge : fixture.disconnected_fan_edges) {
		EXPECT_FALSE(selection_contains(selection, edge));
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, EdgeLoopSelectionUsesFaceContextForSharedCornerEdges) {
	qdr_test::ManifoldCornerLoopFixture fixture =
		qdr_test::make_manifold_corner_loop_document();

	const std::vector<Edge> loop_edges =
		edge_loop_edges(fixture.document, fixture.seed, fixture.top_face);
	Selection loop_selection;
	loop_selection.mode = SelectionMode::Edge;
	loop_selection.edges = loop_edges;

	ASSERT_TRUE(loop_edges.size() == fixture.expected_top_loop_edges.size());
	for (const Edge& edge : fixture.expected_top_loop_edges) {
		EXPECT_TRUE(selection_contains(loop_selection, edge));
	}
	for (const Edge& edge : fixture.side_only_edges) {
		EXPECT_FALSE(selection_contains(loop_selection, edge));
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, EdgeLoopSelectionUsesPickedSurfaceBoundaryForSharedLipEdges) {
	qdr_test::SharedLipLoopFixture fixture =
		qdr_test::make_shared_lip_loop_document();

	const std::vector<Edge> loop_edges =
		edge_loop_edges(fixture.document, fixture.seed,
		                fixture.top_mid_face);
	Selection loop_selection;
	loop_selection.mode = SelectionMode::Edge;
	loop_selection.edges = loop_edges;

	ASSERT_TRUE(loop_edges.size() ==
	            fixture.expected_top_region_loop_edges.size());
	for (const Edge& edge : fixture.expected_top_region_loop_edges) {
		EXPECT_TRUE(selection_contains(loop_selection, edge));
	}
	for (const Edge& edge : fixture.excluded_edges) {
		EXPECT_FALSE(selection_contains(loop_selection, edge));
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, EdgeLoopSelectionKeepsBoundaryHoleLoopWhenPickedFaceContextExists) {
	qdr_test::OpenBoxBoundaryLoopFixture fixture =
		qdr_test::make_open_box_boundary_loop_document();

	const std::vector<Edge> loop_edges =
		edge_loop_edges(fixture.document, fixture.seed, fixture.top_face);
	Selection loop_selection;
	loop_selection.mode = SelectionMode::Edge;
	loop_selection.edges = loop_edges;

	ASSERT_TRUE(loop_edges.size() ==
	            fixture.expected_open_side_loop_edges.size());
	for (const Edge& edge : fixture.expected_open_side_loop_edges) {
		EXPECT_TRUE(selection_contains(loop_selection, edge));
	}
	for (const Edge& edge : fixture.excluded_face_perimeter_edges) {
		EXPECT_FALSE(selection_contains(loop_selection, edge));
	}
}

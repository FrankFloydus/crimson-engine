////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <array>
#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

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

/// Stores a rectangular quad grid and its vertex IDs in row-major order.
struct QuadGridFixture {
	Document document;
	std::size_t columns = 0U;
	std::vector<ElementId> vertices;

	[[nodiscard]] ElementId vertex(std::size_t column,
	                               std::size_t row) const {
		return vertices[row * (columns + 1U) + column];
	}
};

QuadGridFixture make_quad_grid(std::size_t columns, std::size_t rows) {
	QuadGridFixture fixture;
	fixture.columns = columns;
	fixture.vertices.reserve((columns + 1U) * (rows + 1U));
	for (std::size_t row = 0U; row <= rows; ++row) {
		for (std::size_t column = 0U; column <= columns; ++column) {
			fixture.vertices.push_back(add_vertex(
				fixture.document,
				{ static_cast<float>(column), static_cast<float>(row), 0.0F }));
		}
	}
	for (std::size_t row = 0U; row < rows; ++row) {
		for (std::size_t column = 0U; column < columns; ++column) {
			EXPECT_NE(add_face(
			              fixture.document,
			              std::array<ElementId, 4> {
				              fixture.vertex(column, row),
				              fixture.vertex(column + 1U, row),
				              fixture.vertex(column + 1U, row + 1U),
				              fixture.vertex(column, row + 1U),
			              }),
			          kInvalidElementId);
		}
	}
	return fixture;
}

void expect_edge_set(std::span<const Edge> actual,
	                 std::span<const Edge> expected) {
	ASSERT_EQ(actual.size(), expected.size());
	for (const Edge edge : expected) {
		EXPECT_TRUE(std::ranges::find(actual, edge) != actual.end());
	}
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

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeLoopSelectionStopsAtIsolatedQuadOuterCornersAndIgnoresFaceContext) {
	const qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge seed =
		make_edge(fixture.bottom_left, fixture.bottom_right);
	const std::array<Edge, 1> expected { seed };

	expect_edge_set(edge_loop_edges(fixture.document, seed), expected);
	expect_edge_set(
		edge_loop_edges(fixture.document, seed, fixture.face), expected);

	Selection selection;
	select_edge_loop(fixture.document, selection, seed);
	EXPECT_EQ(selection.mode, SelectionMode::Edge);
	expect_edge_set(selection.edges, expected);
	EXPECT_TRUE(selection.has_active);
	EXPECT_EQ(selection.active_kind, ElementKind::Edge);
	EXPECT_EQ(selection.active_edge, seed);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeLoopSelectionStopsAtValenceThreeCubePoles) {
	const Document document = make_cube({});
	const std::vector<Edge> edges = document_edges(document);
	ASSERT_FALSE(edges.empty());
	const Edge seed = edges.front();
	const std::array<Edge, 1> expected { seed };

	expect_edge_set(edge_loop_edges(document, seed), expected);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeLoopSelectionFollowsTopologicalOppositesAcrossRegularQuadGrid) {
	const QuadGridFixture fixture = make_quad_grid(3U, 3U);
	const Edge seed = make_edge(fixture.vertex(1U, 1U),
	                            fixture.vertex(1U, 2U));
	const std::array<Edge, 3> expected {
		make_edge(fixture.vertex(1U, 0U), fixture.vertex(1U, 1U)),
		seed,
		make_edge(fixture.vertex(1U, 2U), fixture.vertex(1U, 3U)),
	};

	expect_edge_set(edge_loop_edges(fixture.document, seed), expected);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeLoopSelectionFollowsStraightBoundaryFanAndStopsAtOuterCorners) {
	const QuadGridFixture fixture = make_quad_grid(3U, 1U);
	const Edge seed = make_edge(fixture.vertex(1U, 1U),
	                            fixture.vertex(2U, 1U));
	const std::array<Edge, 3> expected {
		make_edge(fixture.vertex(0U, 1U), fixture.vertex(1U, 1U)),
		seed,
		make_edge(fixture.vertex(2U, 1U), fixture.vertex(3U, 1U)),
	};

	expect_edge_set(edge_loop_edges(fixture.document, seed), expected);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeLoopSelectionDelimitsBoundaryTransitionsIntoNgons) {
	Document document;
	const ElementId q0 = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId q1 = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId q2 = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const ElementId q3 = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	ASSERT_NE(add_face(document,
	                   std::array<ElementId, 4> { q0, q1, q2, q3 }),
	          kInvalidElementId);
	const ElementId p1 = add_vertex(document, { 2.0F, 0.0F, 0.0F });
	const ElementId p2 = add_vertex(document, { 2.5F, 0.5F, 0.0F });
	const ElementId p3 = add_vertex(document, { 2.0F, 1.5F, 0.0F });
	ASSERT_NE(add_face(document,
	                   std::array<ElementId, 5> { q2, q1, p1, p2, p3 }),
	          kInvalidElementId);

	const Edge quad_seed = make_edge(q0, q1);
	const std::array<Edge, 1> quad_expected { quad_seed };
	expect_edge_set(edge_loop_edges(document, quad_seed), quad_expected);

	const Edge ngon_seed = make_edge(q1, p1);
	const std::array<Edge, 4> ngon_expected {
		ngon_seed,
		make_edge(p1, p2),
		make_edge(p2, p3),
		make_edge(p3, q2),
	};
	expect_edge_set(edge_loop_edges(document, ngon_seed), ngon_expected);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeRingSelectionCrossesTheOppositeEdgeOfAQuad) {
	const qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge seed =
		make_edge(fixture.bottom_left, fixture.bottom_right);
	const std::array<Edge, 2> expected {
		seed,
		make_edge(fixture.top_left, fixture.top_right),
	};

	expect_edge_set(edge_ring_edges(fixture.document, seed), expected);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeRingSelectionStopsAtNgonsAndBeforeNonmanifoldEdges) {
	Document ngon_document;
	const ElementId a =
		add_vertex(ngon_document, { 0.0F, 0.0F, 0.0F });
	const ElementId b =
		add_vertex(ngon_document, { 1.0F, 0.0F, 0.0F });
	const ElementId c =
		add_vertex(ngon_document, { 1.0F, 1.0F, 0.0F });
	const ElementId d =
		add_vertex(ngon_document, { 0.0F, 1.0F, 0.0F });
	ASSERT_NE(add_face(ngon_document,
	                   std::array<ElementId, 4> { a, b, c, d }),
	          kInvalidElementId);
	const ElementId e =
		add_vertex(ngon_document, { 2.0F, -0.5F, 0.0F });
	const ElementId f =
		add_vertex(ngon_document, { 2.5F, 0.5F, 0.0F });
	const ElementId g =
		add_vertex(ngon_document, { 2.0F, 1.5F, 0.0F });
	ASSERT_NE(add_face(ngon_document,
	                   std::array<ElementId, 5> { b, e, f, g, c }),
	          kInvalidElementId);
	const Edge ngon_seed = make_edge(a, d);
	const Edge ngon_delimiter = make_edge(b, c);
	const std::array<Edge, 2> ngon_expected { ngon_seed, ngon_delimiter };
	expect_edge_set(edge_ring_edges(ngon_document, ngon_seed),
	                ngon_expected);

	Document nonmanifold_document;
	const ElementId n0 =
		add_vertex(nonmanifold_document, { 0.0F, 0.0F, 0.0F });
	const ElementId n1 =
		add_vertex(nonmanifold_document, { 1.0F, 0.0F, 0.0F });
	const ElementId n2 =
		add_vertex(nonmanifold_document, { 1.0F, 1.0F, 0.0F });
	const ElementId n3 =
		add_vertex(nonmanifold_document, { 0.0F, 1.0F, 0.0F });
	ASSERT_NE(add_face(nonmanifold_document,
	                   std::array<ElementId, 4> { n0, n1, n2, n3 }),
	          kInvalidElementId);
	const ElementId n4 =
		add_vertex(nonmanifold_document, { 2.0F, 0.0F, 0.0F });
	const ElementId n5 =
		add_vertex(nonmanifold_document, { 2.0F, 1.0F, 0.0F });
	const ElementId n6 =
		add_vertex(nonmanifold_document, { 1.5F, 0.0F, 1.0F });
	const ElementId n7 =
		add_vertex(nonmanifold_document, { 1.5F, 1.0F, 1.0F });
	ASSERT_NE(add_face(nonmanifold_document,
	                   std::array<ElementId, 4> { n1, n4, n5, n2 }),
	          kInvalidElementId);
	ASSERT_NE(add_face(nonmanifold_document,
	                   std::array<ElementId, 4> { n2, n7, n6, n1 }),
	          kInvalidElementId);
	const Edge nonmanifold_seed = make_edge(n0, n3);
	const Edge nonmanifold_delimiter = make_edge(n1, n2);
	ASSERT_EQ(qdr_test::edge_incidence_count(
	              nonmanifold_document, nonmanifold_delimiter),
	          3);
	const std::array<Edge, 1> nonmanifold_expected { nonmanifold_seed };
	expect_edge_set(
		edge_ring_edges(nonmanifold_document, nonmanifold_seed),
		nonmanifold_expected);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     BoundaryComponentTraversalReturnsTheCompleteConnectedBoundaryGraph) {
	const QuadGridFixture fixture = make_quad_grid(3U, 1U);
	const Edge seed = make_edge(fixture.vertex(1U, 1U),
	                            fixture.vertex(2U, 1U));
	const std::array<Edge, 8> expected_bfs {
		seed,
		make_edge(fixture.vertex(0U, 1U), fixture.vertex(1U, 1U)),
		make_edge(fixture.vertex(2U, 1U), fixture.vertex(3U, 1U)),
		make_edge(fixture.vertex(0U, 0U), fixture.vertex(0U, 1U)),
		make_edge(fixture.vertex(3U, 0U), fixture.vertex(3U, 1U)),
		make_edge(fixture.vertex(0U, 0U), fixture.vertex(1U, 0U)),
		make_edge(fixture.vertex(2U, 0U), fixture.vertex(3U, 0U)),
		make_edge(fixture.vertex(1U, 0U), fixture.vertex(2U, 0U)),
	};

	EXPECT_TRUE(edge_boundary_component_edges(fixture.document, seed) ==
	            std::vector<Edge>(expected_bfs.begin(), expected_bfs.end()));
	const Edge manifold_edge =
		make_edge(fixture.vertex(1U, 0U), fixture.vertex(1U, 1U));
	EXPECT_TRUE(edge_boundary_component_edges(fixture.document, manifold_edge)
	                .empty());
	EXPECT_TRUE(edge_boundary_component_edges(
	                fixture.document,
	                { kInvalidElementId, kInvalidElementId })
	                .empty());
}

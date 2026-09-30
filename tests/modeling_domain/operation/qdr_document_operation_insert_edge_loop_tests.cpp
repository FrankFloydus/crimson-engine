////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
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
	ASSERT_EQ(preview.points.size(), 2U);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(
		preview.points[0].position, preview.lines.front().start));
	EXPECT_TRUE(qdr_test::vec_nearly_equal(
		preview.points[1].position, preview.lines.front().end));
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
	ASSERT_EQ(selection.edges.size(), 1U);
	EXPECT_TRUE(qdr_test::edge_positions_match_unordered(
		fixture.document,
		selection.edges.front(),
		{ 0.5F, 0.0F, 0.0F },
		{ 0.5F, 1.0F, 0.0F }));
	const ElementId shared_midpoint = qdr_test::vertex_at_position(
		fixture.document, { 0.5F, 1.0F, 0.0F });
	ASSERT_NE(shared_midpoint, kInvalidElementId);
	std::vector<ElementId> expected_non_quad_vertices = non_quad_vertices_before;
	expected_non_quad_vertices.insert(
		expected_non_quad_vertices.begin() + 1, shared_midpoint);
	EXPECT_EQ(non_quad_face_after->vertices, expected_non_quad_vertices);
	EXPECT_FALSE(qdr_test::document_has_edge(
		fixture.document, fixture.shared_edge));
	EXPECT_EQ(qdr_test::edge_incidence_count(
		fixture.document, make_edge(fixture.shared_edge.a, shared_midpoint)), 2);
	EXPECT_EQ(qdr_test::edge_incidence_count(
		fixture.document, make_edge(shared_midpoint, fixture.shared_edge.b)), 2);
	EXPECT_EQ(qdr_test::edge_incidence_count(
		fixture.document, selection.edges.front()), 2);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
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

std::optional<ElementId> edge_endpoint_at_position(
	const Document& document,
	Edge edge,
	quader::QVec3 position) {
	for (const ElementId vertex_id : std::array { edge.a, edge.b }) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		if (vertex != nullptr &&
		    qdr_test::vec_nearly_equal(vertex->position, position)) {
			return vertex_id;
		}
	}
	return std::nullopt;
}

std::vector<quader::QVec3> compiled_normals_for_vertex(
	const CompiledMesh& mesh,
	ElementId vertex_id) {
	std::vector<quader::QVec3> normals;
	for (const CompiledVertex& vertex : mesh.vertices) {
		if (vertex.source_vertex_id == vertex_id) {
			normals.push_back(vertex.normal);
		}
	}
	return normals;
}

/// Closes a five-quad strip with pentagon caps containing a collinear corner.
struct CappedLoopStripFixture {
	Document document;
	std::array<ElementId, 6> front_left;
	std::array<ElementId, 6> front_right;
	std::array<ElementId, 2> caps;
};

CappedLoopStripFixture make_capped_loop_strip() {
	CappedLoopStripFixture fixture;
	for (std::size_t row = 0U; row < fixture.front_left.size(); ++row) {
		const float height = static_cast<float>(row);
		fixture.front_left[row] = add_vertex(
			fixture.document, { 0.0F, 0.0F, height });
		fixture.front_right[row] = add_vertex(
			fixture.document, { 2.0F, 0.0F, height });
	}
	for (std::size_t row = 0U; row + 1U < fixture.front_left.size(); ++row) {
		EXPECT_NE(add_face(fixture.document, std::array {
			fixture.front_left[row], fixture.front_right[row],
			fixture.front_right[row + 1U], fixture.front_left[row + 1U] }),
			kInvalidElementId);
	}
	const ElementId back_left_bottom = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
	const ElementId back_middle_bottom = add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
	const ElementId back_right_bottom = add_vertex(fixture.document, { 2.0F, 1.0F, 0.0F });
	const ElementId back_left_top = add_vertex(fixture.document, { 0.0F, 1.0F, 5.0F });
	const ElementId back_middle_top = add_vertex(fixture.document, { 1.0F, 1.0F, 5.0F });
	const ElementId back_right_top = add_vertex(fixture.document, { 2.0F, 1.0F, 5.0F });
	std::vector<ElementId> left_side(fixture.front_left.begin(), fixture.front_left.end());
	left_side.insert(left_side.end(), { back_left_top, back_left_bottom });
	EXPECT_NE(add_face(fixture.document, left_side), kInvalidElementId);
	std::vector<ElementId> right_side(fixture.front_right.rbegin(), fixture.front_right.rend());
	right_side.insert(right_side.end(), { back_right_bottom, back_right_top });
	EXPECT_NE(add_face(fixture.document, right_side), kInvalidElementId);
	EXPECT_NE(add_face(fixture.document, std::array {
		back_right_bottom, back_middle_bottom, back_left_bottom,
		back_left_top, back_middle_top, back_right_top }), kInvalidElementId);
	fixture.caps[0] = add_face(fixture.document, std::array {
		fixture.front_left.front(), back_left_bottom, back_middle_bottom,
		back_right_bottom, fixture.front_right.front() });
	fixture.caps[1] = add_face(fixture.document, std::array {
		fixture.front_left.back(), fixture.front_right.back(),
		back_right_top, back_middle_top, back_left_top });
	EXPECT_NE(fixture.caps[0], kInvalidElementId);
	EXPECT_NE(fixture.caps[1], kInvalidElementId);
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
	qdr_test::expect_valid_polygon_document(fixture.document, "closed pentagon-capped loop strip");
	return fixture;
}

} // namespace

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopPreservesSmoothAuthoredShadingAndCompiledContinuity) {
	Document document;
	const ElementId near_left = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId near_right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId bend_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const ElementId bend_left = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId far_right = add_vertex(document, { 1.0F, 1.0F, 1.0F });
	const ElementId far_left = add_vertex(document, { 0.0F, 1.0F, 1.0F });
	ASSERT_NE(add_face(document,
	                   std::array<ElementId, 4> {
		                   near_left, near_right, bend_right, bend_left }),
	          kInvalidElementId);
	ASSERT_NE(add_face(document,
	                   std::array<ElementId, 4> {
		                   bend_left, bend_right, far_right, far_left }),
	          kInvalidElementId);
	for (Face& face : document.faces) {
		face.normal_shading = SurfaceShading::Smooth;
	}
	document.soft_normal_edges = {make_edge(bend_left, bend_right)};
	Selection selection = qdr_test::edge_selection(
		{ make_edge(near_left, near_right) });

	const OperationResult result = insert_edge_loop(document, selection, 0.5F);

	ASSERT_TRUE(result.changed) << result.message;
	qdr_test::expect_valid_polygon_document_after_success(document, result);
	ASSERT_EQ(document.faces.size(), 4U);
	EXPECT_TRUE(std::ranges::all_of(document.faces, [](const Face& face) {
		return face.normal_shading == SurfaceShading::Smooth;
	}));
	ASSERT_EQ(selection.edges.size(), 2U);
	ElementId shared_midpoint = kInvalidElementId;
	for (const ElementId candidate :
	     std::array { selection.edges[0].a, selection.edges[0].b }) {
		if (candidate == selection.edges[1].a ||
		    candidate == selection.edges[1].b) {
			shared_midpoint = candidate;
			break;
		}
	}
	ASSERT_NE(shared_midpoint, kInvalidElementId);
	const CompiledMesh mesh =
		compile_document(document, SurfaceShading::Authored);
	const std::vector<quader::QVec3> midpoint_normals =
		compiled_normals_for_vertex(mesh, shared_midpoint);
	ASSERT_EQ(midpoint_normals.size(), 4U);
	for (const quader::QVec3 normal : midpoint_normals) {
		EXPECT_TRUE(qdr_test::vec_nearly_equal(normal, midpoint_normals.front()));
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopRemapsASplitHardEdgeToBothLiveDescendants) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge hard_seed =
		make_edge(fixture.bottom_left, fixture.bottom_right);
	fixture.document.hard_normal_edges = { hard_seed };
	Selection selection = qdr_test::edge_selection({ hard_seed }, hard_seed);

	const OperationResult result =
		insert_edge_loop(fixture.document, selection, 0.5F);

	ASSERT_TRUE(result.changed) << result.message;
	qdr_test::expect_valid_polygon_document_after_success(
		fixture.document, result);
	ASSERT_EQ(selection.edges.size(), 1U);
	const std::optional<ElementId> midpoint = edge_endpoint_at_position(
		fixture.document,
		selection.edges.front(),
		{ 0.0F, -1.0F, 0.0F });
	ASSERT_TRUE(midpoint.has_value());
	const Edge first_descendant =
		make_edge(fixture.bottom_left, *midpoint);
	const Edge second_descendant =
		make_edge(*midpoint, fixture.bottom_right);
	EXPECT_FALSE(qdr_test::document_has_edge(fixture.document, hard_seed));
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, hard_seed));
	ASSERT_EQ(fixture.document.hard_normal_edges.size(), 2U);
	EXPECT_TRUE(qdr_test::document_has_edge(
		fixture.document, first_descendant));
	EXPECT_TRUE(qdr_test::document_has_edge(
		fixture.document, second_descendant));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, first_descendant));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, second_descendant));
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopRemapsAStoppedTriangleHardEdgeAndUsesItAsTheSecondConnectorDonor) {
	NonQuadLoopPathFixture fixture = make_nonquad_loop_path_fixture(3U);
	fixture.document.hard_normal_edges = { fixture.shared_edge };
	Selection selection =
		qdr_test::edge_selection({ fixture.seed }, fixture.seed);

	const OperationResult result =
		insert_edge_loop(fixture.document, selection, 0.5F);

	ASSERT_TRUE(result.changed) << result.message;
	qdr_test::expect_valid_polygon_document_after_success(
		fixture.document, result);
	ASSERT_EQ(selection.edges.size(), 1U);
	const std::optional<ElementId> midpoint = edge_endpoint_at_position(
		fixture.document,
		selection.edges.front(),
		{ 0.5F, 1.0F, 0.0F });
	ASSERT_TRUE(midpoint.has_value());
	const Edge first_descendant =
		make_edge(fixture.shared_edge.a, *midpoint);
	const Edge second_descendant =
		make_edge(*midpoint, fixture.shared_edge.b);
	ASSERT_EQ(fixture.document.hard_normal_edges.size(), 3U);
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, fixture.shared_edge));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, first_descendant));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, second_descendant));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, selection.edges.front()));
	EXPECT_FALSE(qdr_test::document_has_edge(
		fixture.document, fixture.shared_edge));
	EXPECT_TRUE(qdr_test::document_has_edge(
		fixture.document, first_descendant));
	EXPECT_TRUE(qdr_test::document_has_edge(
		fixture.document, second_descendant));
	EXPECT_EQ(qdr_test::edge_incidence_count(
		fixture.document, first_descendant), 2);
	EXPECT_EQ(qdr_test::edge_incidence_count(
		fixture.document, second_descendant), 2);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
	 InsertEdgeLoopPreservesSoftSplitDescendantsAndLeavesADefaultDonorConnectorDefault) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge soft_seed =
		make_edge(fixture.bottom_left, fixture.bottom_right);
	fixture.document.soft_normal_edges = {soft_seed};
	Selection selection = qdr_test::edge_selection({soft_seed}, soft_seed);

	const OperationResult result =
		insert_edge_loop(fixture.document, selection, 0.5F);

	ASSERT_TRUE(result.changed) << result.message;
	qdr_test::expect_valid_polygon_document_after_success(
		fixture.document, result);
	ASSERT_EQ(selection.edges.size(), 1U);
	const ElementId midpoint = qdr_test::vertex_at_position(
		fixture.document, {0.0F, -1.0F, 0.0F});
	ASSERT_NE(midpoint, kInvalidElementId);
	EXPECT_EQ(fixture.document.soft_normal_edges.size(), 2U);
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.soft_normal_edges,
		make_edge(fixture.bottom_left, midpoint)));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.soft_normal_edges,
		make_edge(midpoint, fixture.bottom_right)));
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, selection.edges.front()));
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.soft_normal_edges, selection.edges.front()));
}

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
		ASSERT_EQ(split_face.uvs.size(), split_face.vertices.size());
		for (std::size_t corner = 0U; corner < split_face.vertices.size(); ++corner) {
			const Vertex* vertex = find_vertex(fixture.document, split_face.vertices[corner]);
			ASSERT_NE(vertex, nullptr);
			EXPECT_FLOAT_EQ(split_face.uvs[corner].x, (vertex->position.x + 1.0F) * 0.5F);
			EXPECT_FLOAT_EQ(split_face.uvs[corner].y, (vertex->position.y + 1.0F) * 0.5F);
		}
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
     PreviewMarkersMatchDistinctCommittedVerticesAcrossOpenAndClosedLoops) {
	for (const bool closed_loop : { false, true }) {
		const Document source = closed_loop ? make_cube({}) : make_quad_grid(1U, 3U).document;
		const Face& face = source.faces.front();
		const Edge seed = make_edge(face.vertices[0], face.vertices[1]);
		for (const float factor : { 0.25F, 0.5F, 0.75F }) {
			SCOPED_TRACE(closed_loop);
			SCOPED_TRACE(factor);
			const OverlayData preview = build_edge_loop_preview(source, seed, factor);
			ASSERT_EQ(preview.lines.size(), closed_loop ? 4U : 3U);
			ASSERT_EQ(preview.points.size(), 4U);
			Document committed = source;
			Selection selection = qdr_test::edge_selection({ seed }, seed);
			const OperationResult result = insert_edge_loop(committed, selection, factor);
			ASSERT_TRUE(result.changed) << result.message;
			ASSERT_EQ(committed.vertices.size() - source.vertices.size(), preview.points.size());
			for (const OverlayPoint& point : preview.points) {
				EXPECT_EQ(std::ranges::count_if(committed.vertices, [&](const Vertex& vertex) {
					return find_vertex(source, vertex.id) == nullptr &&
					       qdr_test::vec_nearly_equal(vertex.position, point.position);
				}), 1);
				EXPECT_EQ(std::ranges::count_if(preview.points, [&](const OverlayPoint& other) {
					return qdr_test::vec_nearly_equal(other.position, point.position);
				}), 1);
			}
		}
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     PreviewMarkerDeduplicationUsesAuthoredEdgeIdentityAtCoincidentPositions) {
	QuadGridFixture fixture = make_quad_grid(1U, 2U);
	for (const std::size_t column : { 0U, 1U }) {
		find_vertex(fixture.document, fixture.vertex(column, 2U))->position =
			find_vertex(fixture.document, fixture.vertex(column, 0U))->position;
	}
	const Edge seed = make_edge(fixture.vertex(0U, 0U), fixture.vertex(1U, 0U));
	const OverlayData preview = build_edge_loop_preview(fixture.document, seed, 0.25F);
	ASSERT_EQ(preview.lines.size(), 2U);
	ASSERT_EQ(preview.points.size(), 3U);
	EXPECT_EQ(std::ranges::count_if(preview.points, [](const OverlayPoint& point) {
		return qdr_test::vec_nearly_equal(point.position, { 0.25F, 0.0F, 0.0F });
	}), 2);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests, LoopInsertionPreservesSeparateChartsAtASharedEdge) {
	QuadGridFixture fixture = make_quad_grid(1U, 2U);
	for (std::size_t index = 0U; index < fixture.document.faces.size(); ++index) {
		Face& face = fixture.document.faces[index];
		face.material_slot = static_cast<std::uint32_t>(index);
		const float offset = static_cast<float>(index) * 100.0F;
		for (const ElementId vertex_id : face.vertices) {
			const auto position = find_vertex(fixture.document, vertex_id)->position;
			face.uvs.push_back({position.x * 4.0F + offset, position.y * 3.0F - offset});
		}
	}
	const Edge seed = make_edge(fixture.vertex(0U, 0U), fixture.vertex(1U, 0U));
	Selection selection = qdr_test::edge_selection({seed}, seed);

	const OperationResult result = insert_edge_loop(fixture.document, selection, 0.25F);

	ASSERT_TRUE(result.changed) << result.message;
	ASSERT_EQ(fixture.document.faces.size(), 4U);
	for (const Face& face : fixture.document.faces) {
		ASSERT_EQ(face.uvs.size(), face.vertices.size());
		const float offset = static_cast<float>(face.material_slot) * 100.0F;
		for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
			const auto position = find_vertex(fixture.document, face.vertices[corner])->position;
			EXPECT_FLOAT_EQ(face.uvs[corner].x, position.x * 4.0F + offset);
			EXPECT_FLOAT_EQ(face.uvs[corner].y, position.y * 3.0F - offset);
		}
	}
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

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     StoppedNonQuadFacesShareCutVerticesAndRetainIndependentCornerAttributes) {
	for (const std::size_t side_count : { 3U, 5U }) {
		for (const bool reverse_winding : { false, true }) {
			for (const float factor : { 0.25F, 0.75F }) {
				SCOPED_TRACE(side_count);
				SCOPED_TRACE(reverse_winding);
				SCOPED_TRACE(factor);
				NonQuadLoopPathFixture fixture = make_nonquad_loop_path_fixture(side_count);
				for (std::size_t index = 0U; index < fixture.document.faces.size(); ++index) {
					Face& face = fixture.document.faces[index];
					face.material_slot = index == 0U ? 7U : 19U;
					face.normal_shading = index == 0U ? SurfaceShading::Smooth : SurfaceShading::Flat;
					face.texture_transform = index == 0U
						? FaceTextureTransform { { -2.0F, 0.5F }, { 0.25F, 0.75F }, 45.0F }
						: FaceTextureTransform { { 0.75F, -3.0F }, { -0.25F, 1.5F }, -17.0F };
					const float offset = index == 0U ? 0.0F : 100.0F;
					for (const ElementId vertex_id : face.vertices) {
						const auto position = find_vertex(fixture.document, vertex_id)->position;
						face.uvs.push_back({ position.x * 4.0F + offset, position.y * 3.0F - offset });
					}
					if (reverse_winding) {
						std::ranges::reverse(face.vertices);
						std::ranges::reverse(face.uvs);
					}
				}
				fixture.document.hard_normal_edges = { fixture.seed };
				fixture.document.soft_normal_edges = { fixture.shared_edge };
				const Document source = fixture.document;
				Selection selection = qdr_test::edge_selection({ fixture.seed }, fixture.seed);

				const OperationResult result = insert_edge_loop(fixture.document, selection, factor);

				ASSERT_TRUE(result.success) << result.message;
				ASSERT_TRUE(result.changed);
				ASSERT_EQ(selection.edges.size(), 1U);
				const ElementId seed_vertex = qdr_test::vertex_at_position(
					fixture.document, { factor, 0.0F, 0.0F });
				const ElementId shared_vertex = qdr_test::vertex_at_position(
					fixture.document, { factor, 1.0F, 0.0F });
				ASSERT_NE(seed_vertex, kInvalidElementId);
				ASSERT_NE(shared_vertex, kInvalidElementId);
				EXPECT_EQ(selection.edges.front(), make_edge(seed_vertex, shared_vertex));
				EXPECT_FALSE(qdr_test::document_has_edge(fixture.document, fixture.seed));
				EXPECT_FALSE(qdr_test::document_has_edge(fixture.document, fixture.shared_edge));
				for (const ElementId endpoint : { fixture.shared_edge.a, fixture.shared_edge.b }) {
					const Edge descendant = make_edge(endpoint, shared_vertex);
					EXPECT_EQ(qdr_test::edge_incidence_count(fixture.document, descendant), 2);
					EXPECT_TRUE(qdr_test::contains_edge(fixture.document.soft_normal_edges, descendant));
				}
				for (const ElementId endpoint : { fixture.seed.a, fixture.seed.b }) {
					const Edge descendant = make_edge(endpoint, seed_vertex);
					EXPECT_EQ(qdr_test::edge_incidence_count(fixture.document, descendant), 1);
					EXPECT_TRUE(qdr_test::contains_edge(fixture.document.hard_normal_edges, descendant));
				}
				EXPECT_FALSE(qdr_test::contains_edge(fixture.document.hard_normal_edges, fixture.seed));
				EXPECT_FALSE(qdr_test::contains_edge(fixture.document.soft_normal_edges, fixture.shared_edge));
				EXPECT_EQ(qdr_test::edge_incidence_count(fixture.document, selection.edges.front()), 2);
				EXPECT_EQ(std::ranges::count_if(fixture.document.faces, [shared_vertex](const Face& face) {
					return std::ranges::find(face.vertices, shared_vertex) != face.vertices.end();
				}), 3);
				const Face* stopped_face = find_face(fixture.document, fixture.non_quad_face);
				ASSERT_NE(stopped_face, nullptr);
				ASSERT_EQ(stopped_face->vertices.size(), side_count + 1U);
				for (const Face& face : fixture.document.faces) {
					const Face& donor = face.id == fixture.non_quad_face ? source.faces[1] : source.faces[0];
					EXPECT_EQ(face.material_slot, donor.material_slot);
					EXPECT_EQ(face.normal_shading, donor.normal_shading);
					EXPECT_EQ(face.texture_transform, donor.texture_transform);
					ASSERT_EQ(face.uvs.size(), face.vertices.size());
					const float offset = face.id == fixture.non_quad_face ? 100.0F : 0.0F;
					for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
						const auto position = find_vertex(fixture.document, face.vertices[corner])->position;
						EXPECT_FLOAT_EQ(face.uvs[corner].x, position.x * 4.0F + offset);
						EXPECT_FLOAT_EQ(face.uvs[corner].y, position.y * 3.0F - offset);
					}
				}
				qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
				EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
			}
		}
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     FiveQuadStripEndingAtCollinearPentagonCapsRemainsClosed) {
	CappedLoopStripFixture fixture = make_capped_loop_strip();
	const Document source = fixture.document;
	const Edge seed = make_edge(fixture.front_left.front(), fixture.front_right.front());
	Selection selection = qdr_test::edge_selection({ seed }, seed);

	const OperationResult result = insert_edge_loop(fixture.document, selection, 0.25F);

	ASSERT_TRUE(result.success) << result.message;
	ASSERT_TRUE(result.changed);
	EXPECT_EQ(fixture.document.vertices.size(), source.vertices.size() + 6U);
	EXPECT_EQ(fixture.document.faces.size(), source.faces.size() + 5U);
	ASSERT_EQ(selection.edges.size(), 5U);
	for (std::size_t row = 0U; row < fixture.front_left.size(); ++row) {
		SCOPED_TRACE(row);
		const Edge split_edge = make_edge(fixture.front_left[row], fixture.front_right[row]);
		const ElementId midpoint = qdr_test::vertex_at_position(
			fixture.document, { 0.5F, 0.0F, static_cast<float>(row) });
		ASSERT_NE(midpoint, kInvalidElementId);
		EXPECT_FALSE(qdr_test::document_has_edge(fixture.document, split_edge));
		EXPECT_EQ(qdr_test::edge_incidence_count(
			fixture.document, make_edge(split_edge.a, midpoint)), 2);
		EXPECT_EQ(qdr_test::edge_incidence_count(
			fixture.document, make_edge(midpoint, split_edge.b)), 2);
		EXPECT_EQ(std::ranges::count_if(fixture.document.faces, [midpoint](const Face& face) {
			return std::ranges::find(face.vertices, midpoint) != face.vertices.end();
		}), row == 0U || row + 1U == fixture.front_left.size() ? 3 : 4);
	}
	for (std::size_t index = 0U; index < fixture.caps.size(); ++index) {
		const Face* cap = find_face(fixture.document, fixture.caps[index]);
		ASSERT_NE(cap, nullptr);
		EXPECT_EQ(cap->vertices.size(), 6U);
		const ElementId endpoint = qdr_test::vertex_at_position(
			fixture.document, { 0.5F, 0.0F, index == 0U ? 0.0F : 5.0F });
		EXPECT_NE(std::ranges::find(cap->vertices, endpoint), cap->vertices.end());
	}
	EXPECT_TRUE(qdr_test::every_edge_is_closed(fixture.document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     UnrelatedCollapsedFacesAndCoincidentLooseVerticesSurviveBoundarySplicing) {
	NonQuadLoopPathFixture fixture = make_nonquad_loop_path_fixture(5U);
	const Document island = qdr_test::append_unrelated_authored_geometry(fixture.document);
	const std::size_t vertex_count = fixture.document.vertices.size();
	const std::size_t face_count = fixture.document.faces.size();
	Selection selection = qdr_test::edge_selection({ fixture.seed }, fixture.seed);

	const OperationResult result = insert_edge_loop(fixture.document, selection, 0.25F);

	ASSERT_TRUE(result.success) << result.message;
	ASSERT_TRUE(result.changed);
	EXPECT_EQ(fixture.document.vertices.size(), vertex_count + 2U);
	EXPECT_EQ(fixture.document.faces.size(), face_count + 1U);
	qdr_test::expect_unrelated_authored_geometry_preserved(fixture.document, island);
	EXPECT_TRUE(validate_polygon_document(fixture.document,
		PolygonDocumentValidationPolicy::AuthoredTopology).ok());
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     InvalidAuthoredBoundaryDataAndNonfiniteFactorsRejectWithoutMutation) {
	for (const qdr_test::AuthoredInputFault fault : qdr_test::kAuthoredInputFaults) {
		SCOPED_TRACE(static_cast<int>(fault));
		NonQuadLoopPathFixture fixture = make_nonquad_loop_path_fixture(5U);
		static_cast<void>(qdr_test::append_unrelated_authored_geometry(fixture.document));
		qdr_test::corrupt_unrelated_authored_geometry(fixture.document, fault);
		Selection selection = qdr_test::edge_selection({ fixture.seed }, fixture.seed);
		const auto source = qdr_test::document_fingerprint(fixture.document);
		const auto source_selection = qdr_test::selection_fingerprint(selection);

		const OperationResult result = insert_edge_loop(fixture.document, selection, 0.25F);

		EXPECT_FALSE(result.success);
		EXPECT_FALSE(result.changed);
		EXPECT_FALSE(result.message.empty());
		EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), source);
		EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
	}
	for (const float factor : { std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		NonQuadLoopPathFixture fixture = make_nonquad_loop_path_fixture(3U);
		Selection selection = qdr_test::edge_selection({ fixture.seed }, fixture.seed);
		const auto source = qdr_test::document_fingerprint(fixture.document);
		const auto source_selection = qdr_test::selection_fingerprint(selection);

		const OperationResult result = insert_edge_loop(fixture.document, selection, factor);

		EXPECT_FALSE(result.success);
		EXPECT_FALSE(result.changed);
		EXPECT_FALSE(result.message.empty());
		EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), source);
		EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     CollapsedWalkedQuadRejectsWithoutPublishingBoundarySplices) {
	NonQuadLoopPathFixture fixture = make_nonquad_loop_path_fixture(5U);
	find_vertex(fixture.document, fixture.seed.a)->position.y = 1.0F;
	find_vertex(fixture.document, fixture.seed.b)->position.y = 1.0F;
	ASSERT_TRUE(validate_polygon_document(fixture.document,
		PolygonDocumentValidationPolicy::AuthoredTopology).ok());
	Selection selection = qdr_test::edge_selection({ fixture.seed }, fixture.seed);
	const auto source = qdr_test::document_fingerprint(fixture.document);
	const auto source_selection = qdr_test::selection_fingerprint(selection);

	const OperationResult result = insert_edge_loop(fixture.document, selection, 0.25F);

	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.changed);
	EXPECT_FALSE(result.message.empty());
	EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), source);
	EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
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
     EdgeLoopSelectionClosesTheFirstAdjacentQuadAtCubePoles) {
	const Document document = make_cube({});
	const std::vector<Edge> edges = document_edges(document);
	ASSERT_FALSE(edges.empty());
	const Edge seed = edges.front();
	const auto first_face = std::ranges::find_if(document.faces, [&](const Face& face) {
		const std::vector<Edge> boundary = qdr_test::face_edges_for_test(face);
		return std::ranges::find(boundary, seed) != boundary.end();
	});
	ASSERT_NE(first_face, document.faces.end());
	const std::vector<Edge> expected = qdr_test::face_edges_for_test(*first_face);

	expect_edge_set(edge_loop_edges(document, seed), expected);
	for (const Face& face : document.faces) {
		expect_edge_set(edge_loop_edges(document, seed, face.id), expected);
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeLoopSelectionFollowsBothCylinderCapPerimetersFromEveryRimEdge) {
	for (const int side_count : { 8, 32 }) {
		for (const bool flip_faces : { false, true }) {
			const Document document = make_cylinder({
				.width = 4.0F, .height = 6.0F, .depth = 2.0F,
				.side_count = side_count, .flip_faces = flip_faces });
			for (const Face& cap : document.faces) {
				if (cap.vertices.size() != static_cast<std::size_t>(side_count)) {
					continue;
				}
				const std::vector<Edge> expected = qdr_test::face_edges_for_test(cap);
				for (const Edge seed : expected) {
					SCOPED_TRACE(seed.a);
					SCOPED_TRACE(seed.b);
					expect_edge_set(edge_loop_edges(document, seed), expected);
					// An edge picked from its side face must still follow the cap.
					for (const Face& face : document.faces) {
						const std::vector<Edge> face_boundary = qdr_test::face_edges_for_test(face);
						if (std::ranges::find(face_boundary, seed) != face_boundary.end()) {
							expect_edge_set(edge_loop_edges(document, seed, face.id), expected);
						}
					}
				}
			}
		}
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     EdgeLoopSelectionClosesOneSideQuadFromEveryCylinderLongitudinalEdge) {
	const Document document = make_cylinder({ .side_count = 16 });
	for (const Edge seed : document_edges(document)) {
		const Vertex* a = find_vertex(document, seed.a);
		const Vertex* b = find_vertex(document, seed.b);
		ASSERT_NE(a, nullptr);
		ASSERT_NE(b, nullptr);
		if (a->position.y == b->position.y) {
			continue;
		}
		SCOPED_TRACE(seed.a);
		SCOPED_TRACE(seed.b);
		const std::vector<Edge> loop = edge_loop_edges(document, seed);
		ASSERT_EQ(loop.size(), 4U);
		EXPECT_EQ(loop.front(), seed);
		EXPECT_TRUE(std::ranges::any_of(document.faces, [&](const Face& face) {
			const std::vector<Edge> boundary = qdr_test::face_edges_for_test(face);
			return boundary.size() == loop.size() &&
			       std::ranges::all_of(boundary, [&](Edge edge) {
				       return std::ranges::find(loop, edge) != loop.end();
			       });
		}));
		EXPECT_EQ(edge_loop_edges(document, { seed.b, seed.a }), loop);

		Selection selected;
		select_edge_loop(document, selected, seed);
		expect_edge_set(selected.edges, loop);
		EXPECT_EQ(selected.active_edge, seed);
		select_edge_loop(document, selected, seed, true);
		EXPECT_TRUE(selected.edges.empty());
	}
}

TEST(QdrDocumentOperationInsertEdgeLoopTests,
     CylinderEdgeRingsRemainOppositeQuadTraversals) {
	const Document document = make_cylinder({ .side_count = 16 });
	std::vector<Edge> longitudinal_edges;
	for (const Edge edge : document_edges(document)) {
		if (find_vertex(document, edge.a)->position.y !=
		    find_vertex(document, edge.b)->position.y) {
			longitudinal_edges.push_back(edge);
		}
	}
	ASSERT_EQ(longitudinal_edges.size(), 16U);
	expect_edge_set(edge_ring_edges(document, longitudinal_edges.front()),
	                longitudinal_edges);
	const Face& side = document.faces.front();
	ASSERT_EQ(side.vertices.size(), 4U);
	std::vector<Edge> rim_pair;
	for (const Edge edge : qdr_test::face_edges_for_test(side)) {
		if (find_vertex(document, edge.a)->position.y ==
		    find_vertex(document, edge.b)->position.y) {
			rim_pair.push_back(edge);
		}
	}
	ASSERT_EQ(rim_pair.size(), 2U);
	expect_edge_set(edge_ring_edges(document, rim_pair.front()), rim_pair);
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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <ranges>

using namespace quader_poly;

TEST(QdrDocumentOperationConnectEdgesTests, ConnectEdgesCutsBetweenOppositeQuadEdgeMidpoints) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const FaceTextureTransform texture{{2.0F, 0.5F}, {0.25F, 0.75F}, 15.0F};
	fixture.document.faces.front().texture_transform = texture;
	fixture.document.faces.front().normal_shading = SurfaceShading::Smooth;
	fixture.document.faces.front().uvs = {
		{0.0F, 0.0F}, {4.0F, 0.0F}, {6.0F, 2.0F}, {0.0F, 4.0F}};
	const Edge bottom_edge = make_edge(fixture.bottom_left, fixture.bottom_right);
	const Edge top_edge = make_edge(fixture.top_right, fixture.top_left);
	fixture.document.hard_normal_edges = {bottom_edge};
	fixture.document.soft_normal_edges = {top_edge};
	const Face source = fixture.document.faces.front();
	Selection selection = qdr_test::edge_selection({
		bottom_edge,
		top_edge,
	});
	const std::size_t vertex_count_before = fixture.document.vertices.size();

	const OperationResult result = connect_selected_edges(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
		EXPECT_EQ(face.normal_shading, SurfaceShading::Smooth);
		ASSERT_EQ(face.uvs.size(), face.vertices.size());
		for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
			const auto before = std::ranges::find(source.vertices, face.vertices[corner]);
			if (before != source.vertices.end()) {
				const auto expected = source.uvs[static_cast<std::size_t>(
					std::distance(source.vertices.begin(), before))];
				EXPECT_EQ(face.uvs[corner].x, expected.x);
				EXPECT_EQ(face.uvs[corner].y, expected.y);
			} else {
				const Vertex* vertex = find_vertex(fixture.document, face.vertices[corner]);
				ASSERT_NE(vertex, nullptr);
				const quader::QVec2 expected = vertex->position.y < 0.0F
					? quader::QVec2{2.0F, 0.0F} : quader::QVec2{3.0F, 3.0F};
				EXPECT_FLOAT_EQ(face.uvs[corner].x, expected.x);
				EXPECT_FLOAT_EQ(face.uvs[corner].y, expected.y);
			}
		}
	}
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(qdr_test::edge_positions_match_unordered(fixture.document, selection.edges.front(), { 0.0F, -1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }));
	const ElementId bottom_midpoint =
		qdr_test::vertex_at_position(fixture.document, {0.0F, -1.0F, 0.0F});
	const ElementId top_midpoint =
		qdr_test::vertex_at_position(fixture.document, {0.0F, 1.0F, 0.0F});
	ASSERT_NE(bottom_midpoint, kInvalidElementId);
	ASSERT_NE(top_midpoint, kInvalidElementId);
	const std::array<Edge, 2> expected_hard_edges{
		make_edge(fixture.bottom_left, bottom_midpoint),
		make_edge(bottom_midpoint, fixture.bottom_right),
	};
	const std::array<Edge, 3> expected_soft_edges{
		make_edge(fixture.top_right, top_midpoint),
		make_edge(top_midpoint, fixture.top_left),
		selection.edges.front(),
	};
	ASSERT_EQ(fixture.document.hard_normal_edges.size(),
	          expected_hard_edges.size());
	for (const Edge edge : expected_hard_edges) {
		EXPECT_TRUE(qdr_test::contains_edge(
			fixture.document.hard_normal_edges, edge));
	}
	ASSERT_EQ(fixture.document.soft_normal_edges.size(),
	          expected_soft_edges.size());
	for (const Edge edge : expected_soft_edges) {
		EXPECT_TRUE(qdr_test::contains_edge(
			fixture.document.soft_normal_edges, edge));
	}
	EXPECT_TRUE(qdr_test::edge_incidence_count(fixture.document, selection.edges.front()) == 2);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationConnectEdgesTests,
	 ConnectEdgesSecondDefaultDonorOverridesTheFirstExplicitMode) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Edge bottom_edge =
		make_edge(fixture.bottom_left, fixture.bottom_right);
	const Edge top_edge = make_edge(fixture.top_right, fixture.top_left);
	fixture.document.soft_normal_edges = {bottom_edge};
	Selection selection = qdr_test::edge_selection({bottom_edge, top_edge});

	const OperationResult result = connect_selected_edges(
		fixture.document, selection);

	ASSERT_TRUE(result.changed) << result.message;
	qdr_test::expect_valid_polygon_document_after_success(
		fixture.document, result);
	ASSERT_EQ(selection.edges.size(), 1U);
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, selection.edges.front()));
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.soft_normal_edges, selection.edges.front()));
	EXPECT_EQ(fixture.document.soft_normal_edges.size(), 2U);
}

TEST(QdrDocumentOperationConnectEdgesTests, ConnectEdgesSplitsSharedCubeEdgesWithoutOpeningTheHull) {
	Document document = make_cube({});
	for (Face& cube_face : document.faces) {
		cube_face.material_slot = static_cast<std::uint32_t>(cube_face.id);
		const float island = static_cast<float>(cube_face.id) * 10.0F;
		cube_face.uvs = {{island, 0.0F}, {island + 2.0F, 0.0F},
		                 {island + 4.0F, 2.0F}, {island, 6.0F}};
	}
	const Document source = document;
	const Face face = document.faces.front();
	Selection selection = qdr_test::edge_selection({
		make_edge(face.vertices[0], face.vertices[1]),
		make_edge(face.vertices[2], face.vertices[3]),
	});
	const std::size_t vertex_count_before = document.vertices.size();

	const OperationResult result = connect_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(document.faces.size() == 7U);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	for (const Face& destination : document.faces) {
		const Face* original = find_face(source, destination.material_slot);
		ASSERT_NE(original, nullptr);
		ASSERT_EQ(destination.uvs.size(), destination.vertices.size());
		for (std::size_t corner = 0U; corner < destination.vertices.size(); ++corner) {
			const ElementId vertex_id = destination.vertices[corner];
			const auto retained = std::ranges::find(original->vertices, vertex_id);
			if (retained != original->vertices.end()) {
				const auto expected = original->uvs[static_cast<std::size_t>(
					std::distance(original->vertices.begin(), retained))];
				EXPECT_EQ(destination.uvs[corner].x, expected.x);
				EXPECT_EQ(destination.uvs[corner].y, expected.y);
				continue;
			}
			const Vertex* midpoint = find_vertex(document, vertex_id);
			ASSERT_NE(midpoint, nullptr);
			bool found_source_edge = false;
			for (std::size_t edge = 0U; edge < original->vertices.size(); ++edge) {
				const std::size_t next = (edge + 1U) % original->vertices.size();
				const auto a = find_vertex(source, original->vertices[edge])->position;
				const auto b = find_vertex(source, original->vertices[next])->position;
				if (!qdr_test::vec_nearly_equal(midpoint->position, (a + b) * 0.5F)) {
					continue;
				}
				const auto expected = (original->uvs[edge] + original->uvs[next]) * 0.5F;
				EXPECT_FLOAT_EQ(destination.uvs[corner].x, expected.x);
				EXPECT_FLOAT_EQ(destination.uvs[corner].y, expected.y);
				found_source_edge = true;
				break;
			}
			EXPECT_TRUE(found_source_edge);
		}
	}
}

TEST(QdrDocumentOperationConnectEdgesTests, ConnectEdgesCutsAnUnorderedWallStripIntoACenterPath) {
	Document document;
	std::array<ElementId, 12> vertices {};
	for (int x = 0; x <= 5; ++x) {
		vertices[static_cast<std::size_t>(x)] = add_vertex(document, { static_cast<float>(x), 0.0F, 0.0F });
		vertices[static_cast<std::size_t>(x + 6)] = add_vertex(document, { static_cast<float>(x), 1.0F, 0.0F });
	}
	for (int x = 0; x < 5; ++x) {
          EXPECT_TRUE(
              add_face(document, std::array<ElementId, 4>{
                                     vertices[static_cast<std::size_t>(x)],
                                     vertices[static_cast<std::size_t>(x + 1)],
                                     vertices[static_cast<std::size_t>(x + 7)],
                                     vertices[static_cast<std::size_t>(x + 6)],
                                 }) != kInvalidElementId);
        }
	Selection selection = qdr_test::edge_selection({
		make_edge(vertices[3], vertices[9]),
		make_edge(vertices[1], vertices[7]),
		make_edge(vertices[4], vertices[10]),
		make_edge(vertices[2], vertices[8]),
	});

	const OperationResult result = connect_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.vertices.size() == 16U);
	EXPECT_TRUE(document.faces.size() == 8U);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 3U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

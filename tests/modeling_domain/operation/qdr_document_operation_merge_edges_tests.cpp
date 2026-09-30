////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <array>

using namespace quader_poly;

namespace {

Document make_parallel_quad_edge_document(Edge& active_edge, Edge& source_edge)
{
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId source_right = add_vertex(document, { 1.0F, 3.0F, 0.0F });
	const ElementId source_left = add_vertex(document, { -1.0F, 3.0F, 0.0F });
	const ElementId source_top_left = add_vertex(document, { -1.0F, 4.0F, 0.0F });
	const ElementId source_top_right = add_vertex(document, { 1.0F, 4.0F, 0.0F });

	const std::array first_face { bottom_left, bottom_right, top_right, top_left };
	const std::array second_face { source_right, source_left, source_top_left, source_top_right };
        EXPECT_TRUE(add_face(document, first_face) != kInvalidElementId);
        EXPECT_TRUE(add_face(document, second_face) != kInvalidElementId);

        active_edge = make_edge(bottom_left, bottom_right);
	source_edge = make_edge(source_right, source_left);
	return document;
}

Document make_separated_triangle_edge_document(
    Edge& active_edge, Edge& source_edge, bool opposite_winding)
{
    Document document;
    const ElementId target_left = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId target_right = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId target_tip = add_vertex(document, {0.0F, 1.0F, 0.0F});
    const ElementId source_left = add_vertex(document, {-1.0F, 2.0F, 0.0F});
    const ElementId source_right = add_vertex(document, {1.0F, 2.0F, 0.0F});
    const ElementId source_tip = add_vertex(document, {0.0F, -1.0F, 0.0F});
    const auto source_loop = opposite_winding
        ? std::array{source_right, source_left, source_tip}
        : std::array{source_left, source_right, source_tip};
    EXPECT_NE(add_face(document, std::array{target_left, target_right, target_tip}, 3),
              kInvalidElementId);
    EXPECT_NE(add_face(document, source_loop, 7), kInvalidElementId);
    document.faces[0].uvs = {{0.25F, 0.5F}, {1.25F, -0.5F}, {-2.0F, 3.0F}};
    document.faces[1].uvs = {{4.0F, -3.0F}, {0.75F, 2.5F}, {-1.5F, 0.125F}};
    document.faces[1].normal_shading = SurfaceShading::Smooth;
    document.faces[1].texture_transform = {{-2.0F, 0.5F}, {0.125F, -0.25F}, 35.0F};
    active_edge = make_edge(target_left, target_right);
    source_edge = make_edge(source_left, source_right);
    return document;
}

} // namespace

TEST(QdrDocumentOperationMergeEdgesTests, RejectsNewMatchingSeamWindingAtomically) {
    Edge active_edge;
    Edge source_edge;
    Document document = make_separated_triangle_edge_document(
        active_edge, source_edge, false);
    static_cast<void>(qdr_test::append_unrelated_authored_geometry(document));
    Selection selection = qdr_test::edge_selection(
        {source_edge, active_edge}, active_edge);
    const auto source = qdr_test::document_fingerprint(document);
    const auto source_selection = qdr_test::selection_fingerprint(selection);

    const auto result = merge_selected_edges(document, selection);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.changed);
    EXPECT_NE(result.message.find("winding"), std::string::npos);
    EXPECT_EQ(qdr_test::document_fingerprint(document), source);
    EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
}

TEST(QdrDocumentOperationMergeEdgesTests,
     RejectsMatchingWindingOnRemappedAdjacentEdgeAtomically) {
    Edge active_edge;
    Edge source_edge;
    Document document = make_separated_triangle_edge_document(
        active_edge, source_edge, true);
    const ElementId target_tip = document.faces[0].vertices[2];
    const ElementId neighbor_tip = add_vertex(document, {0.0F, -2.0F, 0.0F});
    ASSERT_NE(add_face(document,
        std::array{target_tip, source_edge.a, neighbor_tip}, 11), kInvalidElementId);
    Selection selection = qdr_test::edge_selection(
        {source_edge, active_edge}, active_edge);
    const auto source = qdr_test::document_fingerprint(document);
    const auto source_selection = qdr_test::selection_fingerprint(selection);

    const auto result = merge_selected_edges(document, selection);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.changed);
    EXPECT_NE(result.message.find("winding"), std::string::npos);
    EXPECT_EQ(qdr_test::document_fingerprint(document), source);
    EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
}

TEST(QdrDocumentOperationMergeEdgesTests, PreservesUnrelatedMatchingWindingAdjacency) {
    Edge active_edge;
    Edge source_edge;
    Document document = make_separated_triangle_edge_document(
        active_edge, source_edge, true);
    const std::size_t vertex_start = document.vertices.size();
    const std::size_t face_start = document.faces.size();
    const ElementId island_left = add_vertex(document, {10.0F, 0.0F, 0.0F});
    const ElementId island_right = add_vertex(document, {12.0F, 0.0F, 0.0F});
    const ElementId island_upper = add_vertex(document, {11.0F, 1.0F, 0.0F});
    const ElementId island_lower = add_vertex(document, {11.0F, -1.0F, 0.0F});
    ASSERT_NE(add_face(document,
        std::array{island_left, island_right, island_upper}, 13), kInvalidElementId);
    ASSERT_NE(add_face(document,
        std::array{island_left, island_right, island_lower}, 17), kInvalidElementId);
    Document island;
    island.vertices.assign(document.vertices.begin() + vertex_start, document.vertices.end());
    island.faces.assign(document.faces.begin() + face_start, document.faces.end());
    Selection selection = qdr_test::edge_selection(
        {source_edge, active_edge}, active_edge);

    const auto result = merge_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(document.faces[0].vertices[0], active_edge.a);
    EXPECT_EQ(document.faces[0].vertices[1], active_edge.b);
    EXPECT_EQ(document.faces[1].vertices[0], active_edge.b);
    EXPECT_EQ(document.faces[1].vertices[1], active_edge.a);
    EXPECT_EQ(qdr_test::edge_incidence_count(document, active_edge), 2);
    qdr_test::expect_unrelated_authored_geometry_preserved(document, island);
}

TEST(QdrDocumentOperationMergeEdgesTests,
     EqualDistancePairingPreservesActiveIdentityAndFaceAttributes) {
    Edge active_edge;
    Edge source_edge;
    Document document = make_separated_triangle_edge_document(
        active_edge, source_edge, true);
    const ElementId source_tip = document.faces[1].vertices[2];
    find_vertex(document, source_edge.a)->position = {0.0F, 2.0F, 0.0F};
    find_vertex(document, source_edge.b)->position = {0.0F, 4.0F, 0.0F};
    find_vertex(document, source_tip)->position = {-2.0F, 3.0F, 0.0F};
    document.hard_normal_edges = {active_edge, make_edge(source_edge.a, source_tip)};
    document.soft_normal_edges = {make_edge(source_edge.b, source_tip)};
    const Face source_face = document.faces[1];
    const ElementId target_face_id = document.faces[0].id;
    const ElementId next_vertex_id = document.next_vertex_id;
    const ElementId next_face_id = document.next_face_id;
    Selection selection = qdr_test::edge_selection(
        {source_edge, active_edge}, active_edge);

    const auto result = merge_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    const Face* merged_face = find_face(document, source_face.id);
    ASSERT_NE(merged_face, nullptr);
    EXPECT_EQ(merged_face->vertices,
              (std::vector<ElementId>{active_edge.b, active_edge.a, source_tip}));
    EXPECT_EQ(merged_face->material_slot, source_face.material_slot);
    EXPECT_EQ(merged_face->normal_shading, source_face.normal_shading);
    EXPECT_EQ(merged_face->texture_transform, source_face.texture_transform);
    ASSERT_EQ(merged_face->uvs.size(), source_face.uvs.size());
    for (std::size_t index = 0; index < source_face.uvs.size(); ++index) {
        EXPECT_FLOAT_EQ(merged_face->uvs[index].x, source_face.uvs[index].x);
        EXPECT_FLOAT_EQ(merged_face->uvs[index].y, source_face.uvs[index].y);
    }
    EXPECT_NE(find_face(document, target_face_id), nullptr);
    EXPECT_EQ(find_vertex(document, source_edge.a), nullptr);
    EXPECT_EQ(find_vertex(document, source_edge.b), nullptr);
    EXPECT_TRUE(qdr_test::edge_positions_match_unordered(document, active_edge,
        {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(document.hard_normal_edges,
              (std::vector<Edge>{active_edge, make_edge(active_edge.a, source_tip)}));
    EXPECT_EQ(document.soft_normal_edges,
              (std::vector<Edge>{make_edge(active_edge.b, source_tip)}));
    EXPECT_EQ(selection.edges, (std::vector<Edge>{active_edge}));
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_kind, ElementKind::Edge);
    EXPECT_EQ(selection.active_edge, active_edge);
    EXPECT_EQ(document.next_vertex_id, next_vertex_id);
    EXPECT_EQ(document.next_face_id, next_face_id);
}

TEST(QdrDocumentOperationMergeEdgesTests,
     PreservesUnrelatedCollapsedFacesAndLooseVertices) {
    Edge active_edge;
    Edge source_edge;
    Document document = make_parallel_quad_edge_document(active_edge, source_edge);
    const Document island = qdr_test::append_unrelated_authored_geometry(document);
    const auto vertex_count_before = document.vertices.size();
    const auto face_count_before = document.faces.size();
    const auto next_vertex_id_before = document.next_vertex_id;
    const auto next_face_id_before = document.next_face_id;
    Selection selection = qdr_test::edge_selection(
        {active_edge, source_edge}, active_edge);

    const auto result = merge_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(document.vertices.size(), vertex_count_before - 2U);
    EXPECT_EQ(document.faces.size(), face_count_before);
    EXPECT_EQ(find_vertex(document, source_edge.a), nullptr);
    EXPECT_EQ(find_vertex(document, source_edge.b), nullptr);
    EXPECT_TRUE(qdr_test::edge_positions_match_unordered(document, active_edge,
        {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(qdr_test::edge_incidence_count(document, active_edge), 2);
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(document, document.faces[0]));
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(document, document.faces[1]));
    EXPECT_EQ(selection.mode, SelectionMode::Edge);
    EXPECT_EQ(selection.edges, (std::vector<Edge>{active_edge}));
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_kind, ElementKind::Edge);
    EXPECT_EQ(selection.active_edge, active_edge);
    EXPECT_EQ(document.next_vertex_id, next_vertex_id_before);
    EXPECT_EQ(document.next_face_id, next_face_id_before);
    qdr_test::expect_unrelated_authored_geometry_preserved(document, island);
    EXPECT_TRUE(validate_polygon_document(
        document, PolygonDocumentValidationPolicy::AuthoredTopology).ok());
}

TEST(QdrDocumentOperationMergeEdgesTests,
     RejectsMalformedUnrelatedAuthoredDataAtomically) {
    for (const auto fault : qdr_test::kAuthoredInputFaults) {
        SCOPED_TRACE(static_cast<int>(fault));
        Edge active_edge;
        Edge source_edge;
        Document document = make_parallel_quad_edge_document(active_edge, source_edge);
        static_cast<void>(qdr_test::append_unrelated_authored_geometry(document));
        qdr_test::corrupt_unrelated_authored_geometry(document, fault);
        Selection selection = qdr_test::edge_selection(
            {active_edge, source_edge}, active_edge);
        const auto source = qdr_test::document_fingerprint(document);
        const auto source_selection = qdr_test::selection_fingerprint(selection);

        const auto result = merge_selected_edges(document, selection);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(document), source);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
    }
}

TEST(QdrDocumentOperationMergeEdgesTests, MergeEdgesWeldsSourceEndpointsIntoTheClosestSurvivorEdgeVertices) {
	Edge active_edge;
	Edge source_edge;
	Document document = make_parallel_quad_edge_document(active_edge, source_edge);
	Selection selection = qdr_test::edge_selection({ active_edge, source_edge }, active_edge);

	const OperationResult result = merge_selected_edges(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(find_vertex(document, active_edge.a) != nullptr);
	EXPECT_TRUE(find_vertex(document, active_edge.b) != nullptr);
	EXPECT_TRUE(find_vertex(document, source_edge.a) == nullptr);
	EXPECT_TRUE(find_vertex(document, source_edge.b) == nullptr);
	EXPECT_TRUE(document.faces.size() == 2U);
	EXPECT_TRUE(qdr_test::document_has_edge(document, active_edge));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(selection.edges.front() == active_edge);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.active_edge == active_edge);
}

TEST(QdrDocumentOperationMergeEdgesTests, MergeEdgesRejectsIncompleteEdgeSelections) {
	Edge active_edge;
	Edge source_edge;
	Document document = make_parallel_quad_edge_document(active_edge, source_edge);
	Selection selection = qdr_test::edge_selection({ active_edge }, active_edge);

	const OperationResult result = merge_selected_edges(document, selection);

	EXPECT_FALSE(result.changed);
	EXPECT_FALSE(result.message.empty());
}

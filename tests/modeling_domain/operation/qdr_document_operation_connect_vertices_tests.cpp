////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

using namespace quader_poly;

TEST(QdrDocumentOperationConnectVerticesTests, ConnectVerticesSplitsASharedQuadFaceAndSelectsTheNewEdge) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const FaceTextureTransform texture{{2.0F, 0.5F}, {0.25F, -0.75F}, 15.0F};
	fixture.document.faces.front().texture_transform = texture;
	fixture.document.faces.front().normal_shading = SurfaceShading::Smooth;
	fixture.document.faces.front().uvs = {
		{0.0F, 1.0F}, {2.0F, 0.0F}, {3.0F, 4.0F}, {-1.0F, 2.0F}};
	const Edge hard_perimeter =
		make_edge(fixture.bottom_left, fixture.bottom_right);
	const Edge soft_perimeter =
		make_edge(fixture.bottom_right, fixture.top_right);
	fixture.document.hard_normal_edges = {hard_perimeter};
	fixture.document.soft_normal_edges = {soft_perimeter};
	const Face source = fixture.document.faces.front();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left, fixture.top_right });

	const OperationResult result = connect_selected_vertices(fixture.document, selection);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
		EXPECT_EQ(face.normal_shading, SurfaceShading::Smooth);
		ASSERT_EQ(face.uvs.size(), face.vertices.size());
		for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
			const auto source_corner = std::ranges::find(source.vertices, face.vertices[corner]);
			ASSERT_NE(source_corner, source.vertices.end());
			const auto expected = source.uvs[static_cast<std::size_t>(
				std::distance(source.vertices.begin(), source_corner))];
			EXPECT_EQ(face.uvs[corner].x, expected.x);
			EXPECT_EQ(face.uvs[corner].y, expected.y);
		}
	}
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(selection_contains(selection, make_edge(fixture.bottom_left, fixture.top_right)));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, hard_perimeter));
	EXPECT_TRUE(qdr_test::contains_edge(
		fixture.document.soft_normal_edges, soft_perimeter));
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.hard_normal_edges, selection.edges.front()));
	EXPECT_FALSE(qdr_test::contains_edge(
		fixture.document.soft_normal_edges, selection.edges.front()));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationConnectVerticesTests, ConnectVerticesKeepsCubeCutsManifoldForDiagonalAndMultiVertexSelectionsTwoNonAdjacentCubeFaceVerticesCreateOneClosedDiagonal) {
	{
		Document document = make_cube({});
		const Face face = document.faces.front();
		Selection selection = qdr_test::vertex_selection({ face.vertices[0], face.vertices[2] });

		const OperationResult result = connect_selected_vertices(document, selection);

		EXPECT_TRUE(result.changed);
		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(document.faces.size() == 7U);
		EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(face.vertices[0], face.vertices[2])));
                EXPECT_TRUE(selection.mode == SelectionMode::Edge);
                EXPECT_TRUE(selection.edges.size() == 1U);
		EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	}

	
}

TEST(QdrDocumentOperationConnectVerticesTests, ConnectVerticesKeepsCubeCutsManifoldForDiagonalAndMultiVertexSelectionsThreeSelectedCubeFaceVerticesCreateTheClosingSelectedVertexEdgeSequence) {
	

	{
		Document document = make_cube({});
		const Face face = document.faces.front();
		Selection selection = qdr_test::vertex_selection({
			face.vertices[0],
			face.vertices[1],
			face.vertices[2],
		});

		const OperationResult result = connect_selected_vertices(document, selection);

		EXPECT_TRUE(result.changed);
		qdr_test::expect_valid_polygon_document_after_success(document, result);
		EXPECT_TRUE(document.faces.size() == 7U);
		EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(face.vertices[2], face.vertices[0])));
                EXPECT_TRUE(selection.mode == SelectionMode::Edge);
                EXPECT_TRUE(selection.edges.size() == 3U);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
		EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
	}
}

TEST(QdrDocumentOperationConnectVerticesTests, ConnectsIndependentSelectedPairsOnEveryAuthoredFaceAtomically) {
    auto fixture = qdr_test::make_quad_document();
    const std::array second_loop{
        add_vertex(fixture.document, {3.0F, -1.0F, 0.0F}),
        add_vertex(fixture.document, {5.0F, -1.0F, 0.0F}),
        add_vertex(fixture.document, {5.0F, 1.0F, 0.0F}),
        add_vertex(fixture.document, {3.0F, 1.0F, 0.0F})};
    const ElementId second_face_id = add_face(fixture.document, second_loop, 8U);
    const Document island = qdr_test::append_unrelated_authored_geometry(fixture.document);
    Selection selection = qdr_test::vertex_selection({fixture.bottom_left, fixture.top_right,
        second_loop[0], second_loop[2]});

    const auto result = connect_selected_vertices(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(fixture.document.faces.size(), 5U);
    ASSERT_EQ(selection.edges.size(), 2U);
    EXPECT_TRUE(selection_contains(selection, make_edge(fixture.bottom_left, fixture.top_right)));
    EXPECT_TRUE(selection_contains(selection, make_edge(second_loop[0], second_loop[2])));
    EXPECT_NE(find_face(fixture.document, second_face_id), nullptr);
    qdr_test::expect_unrelated_authored_geometry_preserved(fixture.document, island);
}

TEST(QdrDocumentOperationConnectVerticesTests, ConnectsInsertedCollinearBoundaryVerticesWithoutRemovingCorners) {
    auto fixture = qdr_test::make_quad_document();
    const ElementId bottom = add_vertex(fixture.document, {0.0F, -1.0F, 0.0F});
    const ElementId top = add_vertex(fixture.document, {0.0F, 1.0F, 0.0F});
    Face& face = fixture.document.faces.front();
    face.vertices = {fixture.bottom_left, bottom, fixture.bottom_right,
        fixture.top_right, top, fixture.top_left};
    face.uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {2.0F, 0.0F},
        {2.0F, 2.0F}, {1.0F, 2.0F}, {0.0F, 2.0F}};
    const Document source = fixture.document;
    Selection selection = qdr_test::vertex_selection({bottom, top});

    const auto result = connect_selected_vertices(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(fixture.document.faces.size(), 2U);
    EXPECT_EQ(fixture.document.vertices.size(), source.vertices.size());
    for (const Face& replacement : fixture.document.faces) {
        EXPECT_EQ(replacement.vertices.size(), 4U);
        ASSERT_EQ(replacement.uvs.size(), replacement.vertices.size());
    }
    EXPECT_TRUE(selection_contains(selection, make_edge(bottom, top)));
    qdr_test::expect_valid_polygon_document(fixture.document, "inserted boundary vertex connect");
}

TEST(QdrDocumentOperationConnectVerticesTests, RejectsExteriorConcaveChordWithoutPublishingAnotherEligibleFace) {
    auto fixture = qdr_test::make_quad_document();
    const std::array concave_positions{
        quader::QVec3{4.0F, 0.0F, 0.0F}, quader::QVec3{7.0F, 0.0F, 0.0F},
        quader::QVec3{7.0F, 3.0F, 0.0F}, quader::QVec3{6.0F, 3.0F, 0.0F},
        quader::QVec3{6.0F, 1.0F, 0.0F}, quader::QVec3{5.0F, 1.0F, 0.0F},
        quader::QVec3{5.0F, 3.0F, 0.0F}, quader::QVec3{4.0F, 3.0F, 0.0F}};
    std::vector<ElementId> loop;
    for (const auto position : concave_positions) {
        loop.push_back(add_vertex(fixture.document, position));
    }
    ASSERT_NE(add_face(fixture.document, loop), kInvalidElementId);
    Selection selection = qdr_test::vertex_selection({fixture.bottom_left, fixture.top_right, loop[2], loop[7]});
    const auto source = qdr_test::document_fingerprint(fixture.document);
    const auto source_selection = qdr_test::selection_fingerprint(selection);

    const auto result = connect_selected_vertices(fixture.document, selection);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), source);
    EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
}

TEST(QdrDocumentOperationConnectVerticesTests, RejectsMalformedUnrelatedAuthoredDataAtomically) {
    for (const auto fault : qdr_test::kAuthoredInputFaults) {
        auto fixture = qdr_test::make_quad_document();
        static_cast<void>(qdr_test::append_unrelated_authored_geometry(fixture.document));
        qdr_test::corrupt_unrelated_authored_geometry(fixture.document, fault);
        Selection selection = qdr_test::vertex_selection({fixture.bottom_left, fixture.top_right});
        const auto source = qdr_test::document_fingerprint(fixture.document);
        const auto source_selection = qdr_test::selection_fingerprint(selection);
        const auto result = connect_selected_vertices(fixture.document, selection);
        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), source);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
    }
}

TEST(QdrDocumentOperationConnectVerticesTests, DistinguishesInsufficientNoCommonFaceAndAlreadyConnectedSelections) {
    auto fixture = qdr_test::make_quad_document();
    const ElementId loose = add_vertex(fixture.document, {5.0F, 0.0F, 0.0F});
    Selection insufficient = qdr_test::vertex_selection({fixture.bottom_left});
    Selection unrelated = qdr_test::vertex_selection({fixture.bottom_left, loose});
    Selection adjacent = qdr_test::vertex_selection({fixture.bottom_left, fixture.bottom_right});
    const auto first = connect_selected_vertices(fixture.document, insufficient);
    const auto second = connect_selected_vertices(fixture.document, unrelated);
    const auto third = connect_selected_vertices(fixture.document, adjacent);
    EXPECT_FALSE(first.success);
    EXPECT_FALSE(second.success);
    EXPECT_FALSE(third.success);
    EXPECT_NE(first.message, second.message);
    EXPECT_NE(second.message, third.message);
}

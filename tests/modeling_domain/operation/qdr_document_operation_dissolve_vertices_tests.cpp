////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationDissolveVerticesTests, DissolveRetainsAuthoredUvsAtEverySurvivingCorner) {
    Document document;
    const ElementId left = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId middle = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId right = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId top = add_vertex(document, {0.0F, 1.0F, 0.0F});
    const ElementId face_id = add_face(document, std::array{left, middle, right, top});
    Face* face = find_face(document, face_id);
    ASSERT_NE(face, nullptr);
    face->uvs = {{2.0F, 3.0F}, {4.0F, 4.0F}, {6.0F, 5.0F}, {-1.0F, 8.0F}};
    Selection selection = qdr_test::vertex_selection({middle});

    const OperationResult result = dissolve_selected_vertices(document, selection);

    ASSERT_TRUE(result.changed) << result.message;
    face = find_face(document, face_id);
    ASSERT_NE(face, nullptr);
    const std::vector<quader::QVec2> expected{{2.0F, 3.0F}, {6.0F, 5.0F}, {-1.0F, 8.0F}};
    ASSERT_EQ(face->uvs.size(), expected.size());
    for (std::size_t corner = 0; corner < expected.size(); ++corner) {
        EXPECT_FLOAT_EQ(face->uvs[corner].x, expected[corner].x);
        EXPECT_FLOAT_EQ(face->uvs[corner].y, expected[corner].y);
    }
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveVerticesTests, DissolveVerticesRemovesRedundantInlineVerticesAndSelectsNeighbors) {
	Document document;
	const ElementId left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId middle = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top = add_vertex(document, { 0.0F, 1.0F, 0.0F });
        EXPECT_TRUE(add_face(document,
                             std::array<ElementId, 4>{left, middle, right, top},
                             5) != kInvalidElementId);
        Selection selection = qdr_test::vertex_selection({ middle });

	const OperationResult result = dissolve_selected_vertices(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(find_vertex(document, middle) == nullptr);
	EXPECT_TRUE(document.faces.size() == 1U);
	EXPECT_TRUE(document.faces.front().vertices.size() == 3U);
	EXPECT_TRUE(selection_contains(selection, left));
	EXPECT_TRUE(selection_contains(selection, right));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationDissolveVerticesTests, DissolveVerticesRejectsTrueFaceCornersWithoutMutation) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::size_t vertex_count_before = fixture.document.vertices.size();
	const std::size_t face_count_before = fixture.document.faces.size();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left });

	const OperationResult result = dissolve_selected_vertices(fixture.document, selection);

	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == vertex_count_before);
	EXPECT_TRUE(fixture.document.faces.size() == face_count_before);
}

TEST(QdrDocumentOperationDissolveVerticesTests, DissolveRemovesACollinearCornerThatCarriesAUvBreakpoint) {
    Document document;
    const ElementId left = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId middle = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId right = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId top = add_vertex(document, {0.0F, 1.0F, 0.0F});
    const ElementId face_id = add_face(document, std::array{left, middle, right, top});
    Face* face = find_face(document, face_id);
    ASSERT_NE(face, nullptr);
    face->uvs = {{2.0F, 3.0F}, {8.0F, 4.0F}, {6.0F, 5.0F}, {-1.0F, 8.0F}};
    const std::vector<quader::QVec2> expected_uvs{
        face->uvs[0], face->uvs[2], face->uvs[3]};
    Selection selection = qdr_test::vertex_selection({middle});

    const OperationResult result = dissolve_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(find_vertex(document, middle), nullptr);
    face = find_face(document, face_id);
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(face->vertices, (std::vector<ElementId>{left, right, top}));
    ASSERT_EQ(face->uvs.size(), expected_uvs.size());
    for (std::size_t corner = 0; corner < expected_uvs.size(); ++corner) {
        EXPECT_FLOAT_EQ(face->uvs[corner].x, expected_uvs[corner].x);
        EXPECT_FLOAT_EQ(face->uvs[corner].y, expected_uvs[corner].y);
    }
    EXPECT_EQ(selection.vertices, (std::vector<ElementId>{left, right}));
}

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveCompositeEdgeUsesTheLastOrderedNormalModeDonor) {
    Document document;
    const ElementId left = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId middle = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId right = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId top = add_vertex(document, {0.0F, 1.0F, 0.0F});
    ASSERT_NE(add_face(document, std::array{left, middle, right, top}),
              kInvalidElementId);
    document.hard_normal_edges = {make_edge(left, middle)};
    document.soft_normal_edges = {make_edge(middle, right)};
    Selection selection = qdr_test::vertex_selection({middle});

    const OperationResult result =
        dissolve_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    const Edge replacement = make_edge(left, right);
    EXPECT_TRUE(qdr_test::document_has_edge(document, replacement));
    EXPECT_TRUE(qdr_test::contains_edge(document.soft_normal_edges,
                                        replacement));
    EXPECT_FALSE(qdr_test::contains_edge(document.hard_normal_edges,
                                         replacement));
}

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveKeepsAnExistingDefaultReplacementOverAuthoredDonors) {
    Document document;
    const ElementId left = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId middle = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId right = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId top = add_vertex(document, {0.0F, 1.0F, 0.0F});
    const ElementId bottom = add_vertex(document, {0.0F, -1.0F, 0.0F});
    ASSERT_NE(add_face(document, std::array{left, middle, right, top}),
              kInvalidElementId);
    ASSERT_NE(add_face(document, std::array{left, bottom, right}),
              kInvalidElementId);
    document.hard_normal_edges = {make_edge(left, middle)};
    document.soft_normal_edges = {make_edge(middle, right)};
    Selection selection = qdr_test::vertex_selection({middle});

    const OperationResult result =
        dissolve_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    const Edge replacement = make_edge(left, right);
    EXPECT_TRUE(qdr_test::document_has_edge(document, replacement));
    EXPECT_FALSE(qdr_test::contains_edge(document.hard_normal_edges,
                                         replacement));
    EXPECT_FALSE(qdr_test::contains_edge(document.soft_normal_edges,
                                         replacement));
}

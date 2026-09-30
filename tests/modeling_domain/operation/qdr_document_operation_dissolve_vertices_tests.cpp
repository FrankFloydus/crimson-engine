////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

using namespace quader_poly;

namespace {

/// Retains the selected straight corner and its containing face identities.
struct DissolvableCornerFixture {
    Document document;
    ElementId middle = kInvalidElementId;
    ElementId face = kInvalidElementId;
};

DissolvableCornerFixture make_dissolvable_corner_fixture()
{
    DissolvableCornerFixture fixture;
    const ElementId left = add_vertex(fixture.document, {-1.0F, 0.0F, 0.0F});
    fixture.middle = add_vertex(fixture.document, {0.0F, 0.0F, 0.0F});
    const ElementId right = add_vertex(fixture.document, {1.0F, 0.0F, 0.0F});
    const ElementId top = add_vertex(fixture.document, {0.0F, 1.0F, 0.0F});
    fixture.face = add_face(fixture.document, std::array{left, fixture.middle, right, top}, 5);
    Face& face = *find_face(fixture.document, fixture.face);
    face.uvs = {{2.0F, 3.0F}, {4.0F, 4.0F}, {6.0F, 5.0F}, {-1.0F, 8.0F}};
    face.normal_shading = SurfaceShading::Smooth;
    return fixture;
}

} // namespace

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

TEST(QdrDocumentOperationDissolveVerticesTests, DissolvePreservesUnrelatedCollapsedFacesAndLooseVertices) {
    DissolvableCornerFixture fixture = make_dissolvable_corner_fixture();
    const Face source = *find_face(fixture.document, fixture.face);
    const Document island = qdr_test::append_unrelated_authored_geometry(fixture.document);
    Selection selection = qdr_test::vertex_selection({fixture.middle});

    const OperationResult result = dissolve_selected_vertices(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(find_vertex(fixture.document, fixture.middle), nullptr);
    ASSERT_EQ(fixture.document.faces.size(), 2U);
    const Face* face = find_face(fixture.document, fixture.face);
    ASSERT_NE(face, nullptr);
    ASSERT_EQ(face->vertices.size(), 3U);
    ASSERT_EQ(face->uvs.size(), 3U);
    EXPECT_EQ(face->material_slot, source.material_slot);
    EXPECT_EQ(face->normal_shading, source.normal_shading);
    EXPECT_EQ(face->texture_transform, source.texture_transform);
    for (std::size_t corner = 0; corner < face->vertices.size(); ++corner) {
        const auto source_corner = std::ranges::find(source.vertices, face->vertices[corner]);
        ASSERT_NE(source_corner, source.vertices.end());
        const auto index = static_cast<std::size_t>(source_corner - source.vertices.begin());
        EXPECT_FLOAT_EQ(face->uvs[corner].x, source.uvs[index].x);
        EXPECT_FLOAT_EQ(face->uvs[corner].y, source.uvs[index].y);
    }
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(fixture.document, *face));
    EXPECT_EQ(selection.mode, SelectionMode::Vertex);
    EXPECT_EQ(selection.vertices, (std::vector<ElementId>{source.vertices[0], source.vertices[2]}));
    EXPECT_TRUE(validate_polygon_selection_liveness(fixture.document, selection).ok());
    qdr_test::expect_unrelated_authored_geometry_preserved(fixture.document, island);
}

TEST(QdrDocumentOperationDissolveVerticesTests, DissolveRejectsNonfiniteOrMalformedInputAtomically) {
    for (const auto fault : qdr_test::kAuthoredInputFaults) {
        SCOPED_TRACE(static_cast<int>(fault));
        DissolvableCornerFixture fixture = make_dissolvable_corner_fixture();
        static_cast<void>(qdr_test::append_unrelated_authored_geometry(fixture.document));
        qdr_test::corrupt_unrelated_authored_geometry(fixture.document, fault);
        Selection selection = qdr_test::vertex_selection({fixture.middle});
        const std::string original_document = qdr_test::document_fingerprint(fixture.document);
        const std::string original_selection = qdr_test::selection_fingerprint(selection);

        const OperationResult result = dissolve_selected_vertices(fixture.document, selection);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), original_document);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), original_selection);
    }
}

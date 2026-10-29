////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <span>

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

quader::QVec2 dissolve_corner_uv(ElementId vertex) {
    return {2.0F + static_cast<float>(vertex) * 0.25F,
            -3.0F - static_cast<float>(vertex) * 0.5F};
}

void author_dissolve_face(Document& document, ElementId face_id,
                          std::uint32_t material) {
    Face& face = *find_face(document, face_id);
    face.material_slot = material;
    face.normal_shading = SurfaceShading::Smooth;
    face.texture_transform = {{-2.0F, 0.5F}, {0.125F, -0.25F}, 35.0F};
    face.uvs.clear();
    for (const ElementId vertex : face.vertices) {
        face.uvs.push_back(dissolve_corner_uv(vertex));
    }
}

void expect_dissolved_boundary(const Document& document, ElementId face_id,
                               std::span<const ElementId> expected,
                               const Face& source) {
    const Face* face = find_face(document, face_id);
    ASSERT_NE(face, nullptr);
    EXPECT_EQ(face->material_slot, source.material_slot);
    EXPECT_EQ(face->normal_shading, source.normal_shading);
    EXPECT_EQ(face->texture_transform, source.texture_transform);
    ASSERT_EQ(face->vertices.size(), expected.size());
    ASSERT_EQ(face->uvs.size(), expected.size());
    const auto start = std::ranges::find(face->vertices, expected.front());
    ASSERT_NE(start, face->vertices.end());
    const auto offset = static_cast<std::size_t>(start - face->vertices.begin());
    for (std::size_t corner = 0U; corner < expected.size(); ++corner) {
        const auto index = (offset + corner) % expected.size();
        EXPECT_EQ(face->vertices[index], expected[corner]);
        const auto uv = dissolve_corner_uv(expected[corner]);
        EXPECT_FLOAT_EQ(face->uvs[index].x, uv.x);
        EXPECT_FLOAT_EQ(face->uvs[index].y, uv.y);
    }
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(document, *face));
    EXPECT_GT(face_normal(document, *face).z, 0.99F);
}

void expect_surviving_positions_unchanged(const Document& document,
                                         const Document& source) {
    for (const Vertex& vertex : document.vertices) {
        const Vertex* before = find_vertex(source, vertex.id);
        ASSERT_NE(before, nullptr);
        EXPECT_FLOAT_EQ(vertex.position.x, before->position.x);
        EXPECT_FLOAT_EQ(vertex.position.y, before->position.y);
        EXPECT_FLOAT_EQ(vertex.position.z, before->position.z);
    }
    EXPECT_EQ(document.next_vertex_id, source.next_vertex_id);
    EXPECT_EQ(document.next_face_id, source.next_face_id);
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

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveVerticesRemovesNoncollinearQuadCornerWithAuthoredAttributes) {
    qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
    author_dissolve_face(fixture.document, fixture.document.faces.front().id, 7U);
    const Document source = fixture.document;
    const Face source_face = source.faces.front();
    Selection selection = qdr_test::vertex_selection({fixture.bottom_left});

    const OperationResult result = dissolve_selected_vertices(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(find_vertex(fixture.document, fixture.bottom_left), nullptr);
    ASSERT_EQ(fixture.document.vertices.size(), 3U);
    ASSERT_EQ(fixture.document.faces.size(), 1U);
    const std::array boundary{fixture.bottom_right, fixture.top_right, fixture.top_left};
    expect_dissolved_boundary(fixture.document, source_face.id, boundary, source_face);
    expect_surviving_positions_unchanged(fixture.document, source);
    EXPECT_EQ(selection.mode, SelectionMode::Vertex);
    EXPECT_TRUE(selection_contains(selection, fixture.bottom_right));
    EXPECT_TRUE(selection_contains(selection, fixture.top_left));
    EXPECT_EQ(selection.vertices.size(), 2U);
    EXPECT_TRUE(validate_polygon_selection_liveness(fixture.document, selection).ok());
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveBentDegreeTwoSharedSeamKeepsBothIncidentFaces) {
    Document document;
    const ElementId left = add_vertex(document, {0, 0, 0});
    const ElementId middle = add_vertex(document, {1, 0.25F, 0});
    const ElementId right = add_vertex(document, {2, 0, 0});
    const ElementId top = add_vertex(document, {1, 2, 0});
    const ElementId bottom = add_vertex(document, {1, -2, 0});
    const ElementId upper = add_face(document, std::array{left, middle, right, top});
    const ElementId lower = add_face(document, std::array{right, middle, left, bottom});
    author_dissolve_face(document, upper, 7U);
    author_dissolve_face(document, lower, 11U);
    const Document source = document;
    Selection selection = qdr_test::vertex_selection({middle});

    const OperationResult result = dissolve_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(find_vertex(document, middle), nullptr);
    ASSERT_EQ(document.vertices.size(), 4U);
    ASSERT_EQ(document.faces.size(), 2U);
    const std::array upper_boundary{left, right, top};
    const std::array lower_boundary{right, left, bottom};
    expect_dissolved_boundary(document, upper, upper_boundary, source.faces[0]);
    expect_dissolved_boundary(document, lower, lower_boundary, source.faces[1]);
    EXPECT_TRUE(qdr_test::document_has_edge(document, make_edge(left, right)));
    expect_surviving_positions_unchanged(document, source);
    EXPECT_EQ(selection.vertices, (std::vector<ElementId>{left, right}));
    EXPECT_TRUE(validate_polygon_selection_liveness(document, selection).ok());
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveInteriorFanJoinsFourTrianglesAndRetainsUnrelatedAuthoredGeometry) {
    Document document;
    const ElementId a = add_vertex(document, {-1, -1, 0});
    const ElementId b = add_vertex(document, {1, -1, 0});
    const ElementId c = add_vertex(document, {1, 1, 0});
    const ElementId d = add_vertex(document, {-1, 1, 0});
    const ElementId center = add_vertex(document, {0.2F, 0.1F, 0});
    const ElementId first = add_face(document, std::array{a, b, center});
    const ElementId second = add_face(document, std::array{b, c, center});
    const ElementId third = add_face(document, std::array{c, d, center});
    const ElementId fourth = add_face(document, std::array{d, a, center});
    for (const auto id : {first, second, third, fourth}) {
        author_dissolve_face(document, id, id == first ? 7U : 11U);
    }
    const Face first_source = *find_face(document, first);
    const Document island = qdr_test::append_unrelated_authored_geometry(document);
    const Document source = document;
    Selection selection = qdr_test::vertex_selection({center});

    const OperationResult result = dissolve_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(find_vertex(document, center), nullptr);
    ASSERT_EQ(document.faces.size(), 2U);
    EXPECT_EQ(find_face(document, second), nullptr);
    EXPECT_EQ(find_face(document, third), nullptr);
    EXPECT_EQ(find_face(document, fourth), nullptr);
    const std::array boundary{a, b, c, d};
    expect_dissolved_boundary(document, first, boundary, first_source);
    expect_surviving_positions_unchanged(document, source);
    auto neighbors = selection.vertices;
    std::ranges::sort(neighbors);
    EXPECT_EQ(neighbors, (std::vector<ElementId>{a, b, c, d}));
    EXPECT_TRUE(validate_polygon_selection_liveness(document, selection).ok());
    qdr_test::expect_unrelated_authored_geometry_preserved(document, island);
}

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveBoundaryJunctionJoinsFacesWithoutRemovingUnselectedStraightCorner) {
    Document document;
    const ElementId a = add_vertex(document, {-1, 0, 0});
    const ElementId junction = add_vertex(document, {0, -0.25F, 0});
    const ElementId b = add_vertex(document, {1, 0, 0});
    const ElementId c = add_vertex(document, {1, 1, 0});
    const ElementId seam_end = add_vertex(document, {0, 1, 0});
    const ElementId d = add_vertex(document, {-1, 1, 0});
    const ElementId first = add_face(document, std::array{a, junction, seam_end, d});
    const ElementId second = add_face(document, std::array{junction, b, c, seam_end});
    author_dissolve_face(document, first, 7U);
    author_dissolve_face(document, second, 11U);
    document.hard_normal_edges = {make_edge(a, junction)};
    document.soft_normal_edges = {make_edge(junction, b)};
    const Document source = document;
    Selection selection = qdr_test::vertex_selection({junction});

    const OperationResult result = dissolve_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(find_vertex(document, junction), nullptr);
    ASSERT_EQ(document.vertices.size(), 5U);
    ASSERT_EQ(document.faces.size(), 1U);
    EXPECT_EQ(find_face(document, second), nullptr);
    const std::array boundary{a, b, c, seam_end, d};
    expect_dissolved_boundary(document, first, boundary, source.faces[0]);
    expect_surviving_positions_unchanged(document, source);
    EXPECT_NE(find_vertex(document, seam_end), nullptr);
    const auto replacement = make_edge(a, b);
    EXPECT_TRUE(qdr_test::document_has_edge(document, replacement));
    EXPECT_TRUE(qdr_test::contains_edge(document.soft_normal_edges, replacement));
    EXPECT_FALSE(qdr_test::contains_edge(document.hard_normal_edges, replacement));
    auto neighbors = selection.vertices;
    std::ranges::sort(neighbors);
    EXPECT_EQ(neighbors, (std::vector<ElementId>{a, b, seam_end}));
    EXPECT_TRUE(validate_polygon_selection_liveness(document, selection).ok());
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveAdjacentInteriorJunctionsIsIndependentOfSelectionOrder) {
    std::string first_result;
    for (const bool reverse_selection : {false, true}) {
        SCOPED_TRACE(reverse_selection);
        Document document;
        std::array<std::array<ElementId, 4>, 3> grid{};
        for (std::size_t row = 0U; row < grid.size(); ++row) {
            for (std::size_t column = 0U; column < grid[row].size(); ++column) {
                grid[row][column] = add_vertex(document,
                    {static_cast<float>(column), static_cast<float>(row), 0});
            }
        }
        for (std::size_t row = 0U; row < 2U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                const auto id = add_face(document, std::array{grid[row][column],
                    grid[row][column + 1U], grid[row + 1U][column + 1U],
                    grid[row + 1U][column]});
                author_dissolve_face(document, id, id == 1U ? 7U : 11U);
            }
        }
        const Document source = document;
        Selection selection = reverse_selection
            ? qdr_test::vertex_selection({grid[1][2], grid[1][1]})
            : qdr_test::vertex_selection({grid[1][1], grid[1][2]});

        const OperationResult result = dissolve_selected_vertices(document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(document.faces.size(), 1U);
        ASSERT_EQ(document.vertices.size(), 10U);
        EXPECT_EQ(find_vertex(document, grid[1][1]), nullptr);
        EXPECT_EQ(find_vertex(document, grid[1][2]), nullptr);
        const std::array boundary{grid[0][0], grid[0][1], grid[0][2], grid[0][3],
            grid[1][3], grid[2][3], grid[2][2], grid[2][1], grid[2][0], grid[1][0]};
        expect_dissolved_boundary(document, source.faces.front().id, boundary,
                                  source.faces.front());
        expect_surviving_positions_unchanged(document, source);
        auto neighbors = selection.vertices;
        std::ranges::sort(neighbors);
        EXPECT_EQ(neighbors, (std::vector<ElementId>{grid[0][1], grid[0][2],
            grid[1][0], grid[1][3], grid[2][1], grid[2][2]}));
        EXPECT_TRUE(validate_polygon_selection_liveness(document, selection).ok());
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        const auto fingerprint = qdr_test::document_fingerprint(document);
        if (reverse_selection) EXPECT_EQ(fingerprint, first_result);
        else first_result = fingerprint;
    }
}

TEST(QdrDocumentOperationDissolveVerticesTests,
     DissolveRejectsFewerThanThreeSurvivingCornersAtomically) {
    for (const bool triangle : {false, true}) {
        SCOPED_TRACE(triangle);
        auto fixture = qdr_test::make_quad_document();
        if (triangle) fixture.document.faces.front().vertices.pop_back();
        author_dissolve_face(fixture.document, fixture.document.faces.front().id, 7U);
        static_cast<void>(qdr_test::append_unrelated_authored_geometry(fixture.document));
        Selection selection = triangle
            ? qdr_test::vertex_selection({fixture.bottom_left})
            : qdr_test::vertex_selection({fixture.bottom_left, fixture.bottom_right});
        const auto before_document = qdr_test::document_fingerprint(fixture.document);
        const auto before_selection = qdr_test::selection_fingerprint(selection);

        const OperationResult result = dissolve_selected_vertices(fixture.document, selection);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), before_document);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), before_selection);
    }
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

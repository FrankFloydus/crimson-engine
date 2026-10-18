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
#include <span>

using namespace quader_poly;

namespace {

Document saved_folded_connect_source() {
    // Exact authored geometry from SEL-20261010-EA5C510E. Face 5 folds from
    // the box roof into its side; 3--7 crosses 2--14 only in the XZ projection.
    Document document;
    document.vertices = {
        {1, {-2.0F, -0.5F, 2.0F}},
        {2, {1.6442902088165283F, -0.5F, 1.6442902088165283F}},
        {3, {2.0F, 3.4893150329589844F, 2.0F}},
        {4, {-2.0F, 3.4893150329589844F, 2.0F}},
        {5, {-2.0F, -0.5F, -2.0F}},
        {6, {-2.0F, 3.4893150329589844F, -2.0F}},
        {7, {2.0F, 3.4893150329589844F, -2.0F}},
        {8, {2.0F, -0.5F, -2.0F}},
        {14, {2.9748735427856445F, 4.489315032958984F, -1.1005066633224487F}},
    };
    document.faces = {
        {.id = 1, .vertices = {1, 2, 3, 4}},
        {.id = 2, .vertices = {5, 6, 7, 8}},
        {.id = 4, .vertices = {5, 1, 4, 6}},
        {.id = 6, .vertices = {5, 8, 2, 1}},
        {.id = 5, .vertices = {2, 14, 7, 6, 4, 3}},
    };
    document.next_vertex_id = 15;
    document.next_face_id = 10;
    return document;
}

Document saved_folded_connect_after_roof_cut() {
    // SEL-20261010-563C461F captures the first dump after connecting 3--7.
    Document document = saved_folded_connect_source();
    find_face(document, 5)->vertices = {7, 6, 4, 3};
    document.faces.push_back({.id = 10, .vertices = {3, 2, 14, 7}});
    document.next_face_id = 11;
    return document;
}

constexpr std::array<Edge, 16> kOriginalFoldedConnections{{
    {1, 3}, {1, 6}, {1, 8}, {2, 4}, {2, 5}, {2, 6}, {2, 7}, {3, 6},
    {3, 7}, {3, 14}, {4, 5}, {4, 7}, {4, 14}, {5, 7}, {6, 8}, {6, 14},
}};

constexpr std::array<Edge, 11> kRoofCutConnections{{
    {1, 3}, {1, 6}, {1, 8}, {2, 4}, {2, 5}, {2, 7},
    {3, 6}, {4, 5}, {4, 7}, {5, 7}, {6, 8},
}};

std::vector<Edge> every_vertex_pair(const Document& document) {
    std::vector<Edge> pairs;
    for (std::size_t first = 0; first < document.vertices.size(); ++first) {
        for (std::size_t second = first + 1U; second < document.vertices.size(); ++second) {
            pairs.push_back(make_edge(document.vertices[first].id, document.vertices[second].id));
        }
    }
    return pairs;
}

void expect_one_connected_face(const Document& source, const Document& document, Edge connector) {
    ASSERT_EQ(document.vertices.size(), source.vertices.size());
    EXPECT_EQ(document.next_vertex_id, source.next_vertex_id);
    EXPECT_EQ(document.faces.size(), source.faces.size() + 1U);
    for (const Vertex& original : source.vertices) {
        const Vertex* vertex = find_vertex(document, original.id);
        ASSERT_NE(vertex, nullptr);
        EXPECT_EQ(vertex->position.x, original.position.x);
        EXPECT_EQ(vertex->position.y, original.position.y);
        EXPECT_EQ(vertex->position.z, original.position.z);
    }
    std::size_t changed_faces = 0;
    for (const Face& original : source.faces) {
        const Face* face = find_face(document, original.id);
        ASSERT_NE(face, nullptr);
        changed_faces += face->vertices != original.vertices;
    }
    EXPECT_EQ(changed_faces, 1U);
    EXPECT_EQ(qdr_test::edge_incidence_count(document, connector), 2);
    for (const Edge edge : document_edges(source)) {
        EXPECT_EQ(qdr_test::edge_incidence_count(document, edge),
            qdr_test::edge_incidence_count(source, edge));
    }
    EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
    const auto validation = validate_polygon_document(document,
        PolygonDocumentValidationPolicy::AuthoredTopology);
    EXPECT_TRUE(validation.ok());
}

std::vector<Edge> rendered_interior_diagonals(const Document& document) {
    std::vector<Edge> diagonals;
    const auto boundary_edges = document_edges(document);
    for (const Face& face : document.faces) {
        Document isolated;
        isolated.vertices = document.vertices;
        isolated.faces = {face};
        const auto mesh = compile_document(isolated);
        std::map<std::pair<ElementId, ElementId>, unsigned> incidence;
        for (std::size_t triangle = 0; triangle + 2U < mesh.indices.size(); triangle += 3U) {
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                const auto first = mesh.indices[triangle + corner];
                const auto second = mesh.indices[triangle + (corner + 1U) % 3U];
                if (first >= mesh.vertices.size() || second >= mesh.vertices.size()) {
                    ADD_FAILURE() << "Compiled triangle references a missing corner.";
                    return {};
                }
                const Edge edge = make_edge(mesh.vertices[first].source_vertex_id,
                    mesh.vertices[second].source_vertex_id);
                ++incidence[{edge.a, edge.b}];
            }
        }
        const auto face_boundary = document_edges(isolated);
        const bool complete_boundary = std::ranges::all_of(face_boundary, [&](Edge edge) {
            const auto found = incidence.find({edge.a, edge.b});
            return found != incidence.end() && found->second == 1U;
        }) && std::ranges::all_of(incidence, [&](const auto& entry) {
            const Edge edge = make_edge(entry.first.first, entry.first.second);
            const bool boundary = std::ranges::find(face_boundary, edge) != face_boundary.end();
            return entry.second == (boundary ? 1U : 2U);
        });
        if (!complete_boundary) {
            continue;
        }
        for (const auto& [endpoints, count] : incidence) {
            const Edge edge = make_edge(endpoints.first, endpoints.second);
            if (count == 2U && std::ranges::find(boundary_edges, edge) == boundary_edges.end() &&
                std::ranges::find(diagonals, edge) == diagonals.end()) {
                diagonals.push_back(edge);
            }
        }
    }
    return diagonals;
}

quader::QVec3 oblique_position(quader::QVec3 position, float scale) {
    const float rotated_x = 0.6F * position.x - 0.8F * position.z;
    const float rotated_z = 0.8F * position.x + 0.6F * position.z;
    return {13.0F + scale * rotated_x,
        -7.0F + scale * (0.8F * position.y - 0.6F * rotated_z),
        5.0F + scale * (0.6F * position.y + 0.8F * rotated_z)};
}

bool same_oriented_loop(std::span<const ElementId> actual,
                        std::span<const ElementId> expected) {
    if (actual.size() != expected.size() || expected.empty()) {
        return false;
    }
    const auto first = std::ranges::find(actual, expected.front());
    if (first == actual.end()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(std::distance(actual.begin(), first));
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual[(offset + index) % actual.size()] != expected[index]) {
            return false;
        }
    }
    return true;
}

void expect_folded_connect_split(const Document& document, bool reversed = false) {
    std::array<ElementId, 4> roof{7, 6, 4, 3};
    std::array<ElementId, 4> side{3, 2, 14, 7};
    if (reversed) {
        std::ranges::reverse(roof);
        std::ranges::reverse(side);
    }
    const Face* roof_face = find_face(document, 5);
    const Face* side_face = find_face(document, 10);
    ASSERT_NE(roof_face, nullptr);
    ASSERT_NE(side_face, nullptr);
    EXPECT_TRUE(same_oriented_loop(roof_face->vertices, roof));
    EXPECT_TRUE(same_oriented_loop(side_face->vertices, side));
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(document, *roof_face));
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(document, *side_face));
}

} // namespace

TEST(QdrDocumentOperationConnectVerticesTests, EveryPairInBothSavedFoldedSnapshotsHasAnExplicitOutcome) {
    for (const bool roof_cut : {false, true}) {
        const Document source = roof_cut
            ? saved_folded_connect_after_roof_cut() : saved_folded_connect_source();
        const std::span<const Edge> accepted = roof_cut
            ? std::span<const Edge>(kRoofCutConnections) : std::span<const Edge>(kOriginalFoldedConnections);
        const auto pairs = every_vertex_pair(source);
        ASSERT_EQ(pairs.size(), 36U);
        for (const Edge pair : pairs) {
            for (const bool reverse_selection : {false, true}) {
                SCOPED_TRACE(::testing::Message() << "roof_cut=" << roof_cut << " pair=" << pair.a
                    << ',' << pair.b << " reverse_selection=" << reverse_selection);
                Document document = source;
                Selection selection = reverse_selection
                    ? qdr_test::vertex_selection({pair.b, pair.a})
                    : qdr_test::vertex_selection({pair.a, pair.b});
                const auto source_selection = qdr_test::selection_fingerprint(selection);
                const bool expected_success = std::ranges::find(accepted, pair) != accepted.end();

                const auto result = connect_selected_vertices(document, selection);

                ASSERT_EQ(result.success, expected_success) << result.message;
                EXPECT_EQ(result.changed, expected_success);
                if (expected_success) {
                    expect_one_connected_face(source, document, pair);
                    EXPECT_EQ(selection.edges, (std::vector<Edge>{pair}));
                } else {
                    EXPECT_EQ(qdr_test::document_fingerprint(document), qdr_test::document_fingerprint(source));
                    EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
                }
            }
        }
    }
}

TEST(QdrDocumentOperationConnectVerticesTests, EverySecondPairPreservesTopologyAndAcceptsExistingRenderDiagonals) {
    for (const bool roof_cut : {false, true}) {
        const Document source = roof_cut
            ? saved_folded_connect_after_roof_cut() : saved_folded_connect_source();
        const std::span<const Edge> accepted = roof_cut
            ? std::span<const Edge>(kRoofCutConnections) : std::span<const Edge>(kOriginalFoldedConnections);
        const auto pairs = every_vertex_pair(source);
        for (const Edge first : accepted) {
            Document once_cut = source;
            Selection first_selection = qdr_test::vertex_selection({first.a, first.b});
            const auto first_result = connect_selected_vertices(once_cut, first_selection);
            ASSERT_TRUE(first_result.success) << first_result.message;
            const auto rendered_diagonals = rendered_interior_diagonals(once_cut);
            for (const Edge second : pairs) {
                SCOPED_TRACE(::testing::Message() << "roof_cut=" << roof_cut << " first=" << first.a
                    << ',' << first.b << " second=" << second.a << ',' << second.b);
                Document document = once_cut;
                Selection selection = qdr_test::vertex_selection({second.a, second.b});
                const auto source_selection = qdr_test::selection_fingerprint(selection);

                const auto result = connect_selected_vertices(document, selection);

                if (std::ranges::find(rendered_diagonals, second) != rendered_diagonals.end()) {
                    EXPECT_TRUE(result.success) << result.message;
                }
                if (result.success) {
                    EXPECT_TRUE(result.changed);
                    expect_one_connected_face(once_cut, document, second);
                    EXPECT_EQ(selection.edges, (std::vector<Edge>{second}));
                } else {
                    EXPECT_FALSE(result.changed);
                    EXPECT_EQ(qdr_test::document_fingerprint(document), qdr_test::document_fingerprint(once_cut));
                    EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
                }
            }
        }
    }
}

TEST(QdrDocumentOperationConnectVerticesTests, RenderedDiagonalsRemainConnectableAcrossLoopOriginsWindingAndTransforms) {
    for (const bool roof_cut : {false, true}) {
        const std::size_t origin_count = roof_cut ? 4U : 6U;
        for (std::size_t origin = 0; origin < origin_count; ++origin) {
            for (const bool reversed : {false, true}) {
                for (const float scale : {0.125F, 1.0F, 8.0F}) {
                    Document source = roof_cut
                        ? saved_folded_connect_after_roof_cut() : saved_folded_connect_source();
                    for (Vertex& vertex : source.vertices) {
                        vertex.position = oblique_position(vertex.position, scale);
                    }
                    for (Face& face : source.faces) {
                        if (reversed) {
                            std::ranges::reverse(face.vertices);
                        }
                        std::ranges::rotate(face.vertices, face.vertices.begin() +
                            static_cast<std::ptrdiff_t>(origin % face.vertices.size()));
                    }
                    const auto diagonals = rendered_interior_diagonals(source);
                    ASSERT_FALSE(diagonals.empty());
                    const auto boundary_edges = document_edges(source);
                    for (const Edge pair : every_vertex_pair(source)) {
                        SCOPED_TRACE(::testing::Message() << "roof_cut=" << roof_cut << " origin=" << origin
                            << " reversed=" << reversed << " scale=" << scale << " pair=" << pair.a << ',' << pair.b);
                        Document document = source;
                        Selection selection = qdr_test::vertex_selection({pair.b, pair.a});
                        const auto source_selection = qdr_test::selection_fingerprint(selection);

                        const auto result = connect_selected_vertices(document, selection);

                        if (std::ranges::find(diagonals, pair) != diagonals.end()) {
                            EXPECT_TRUE(result.success) << result.message;
                        }
                        if (std::ranges::find(boundary_edges, pair) != boundary_edges.end()) {
                            EXPECT_FALSE(result.success);
                        }
                        if (result.success) {
                            EXPECT_TRUE(result.changed);
                            expect_one_connected_face(source, document, pair);
                            EXPECT_EQ(selection.edges, (std::vector<Edge>{pair}));
                        } else {
                            EXPECT_FALSE(result.changed);
                            EXPECT_EQ(qdr_test::document_fingerprint(document), qdr_test::document_fingerprint(source));
                            EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
                        }
                    }
                }
            }
        }
    }
}

TEST(QdrDocumentOperationConnectVerticesTests, SplitsOnlyOneCommonFaceInEitherStorageOrder) {
    for (const bool reverse_faces : {false, true}) {
        SCOPED_TRACE(reverse_faces);
        Document document = saved_folded_connect_source();
        if (reverse_faces) {
            std::ranges::reverse(document.faces);
        }
        const Document source = document;
        Selection selection = qdr_test::vertex_selection({2, 4});

        const auto result = connect_selected_vertices(document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        expect_one_connected_face(source, document, make_edge(2, 4));
        const ElementId skipped_face = reverse_faces ? 1 : 5;
        ASSERT_NE(find_face(document, skipped_face), nullptr);
        EXPECT_EQ(find_face(document, skipped_face)->vertices,
            find_face(source, skipped_face)->vertices);
    }
}

TEST(QdrDocumentOperationConnectVerticesTests, TriesAnotherCommonFaceAfterTheFirstRejectsTheChord) {
    Document document;
    const std::array positions{
        quader::QVec3{0.0F, 0.0F, 0.0F}, quader::QVec3{3.0F, 0.0F, 0.0F},
        quader::QVec3{3.0F, 3.0F, 0.0F}, quader::QVec3{2.0F, 3.0F, 0.0F},
        quader::QVec3{2.0F, 1.0F, 0.0F}, quader::QVec3{1.0F, 1.0F, 0.0F},
        quader::QVec3{1.0F, 3.0F, 0.0F}, quader::QVec3{0.0F, 3.0F, 0.0F}};
    std::vector<ElementId> concave_loop;
    for (const auto position : positions) {
        concave_loop.push_back(add_vertex(document, position));
    }
    const ElementId concave_id = add_face(document, concave_loop);
    ASSERT_NE(concave_id, kInvalidElementId);
    const ElementId first = concave_loop[3];
    const ElementId second = concave_loop[6];
    const ElementId above = add_vertex(document, {1.5F, 3.0F, 1.0F});
    const ElementId below = add_vertex(document, {1.5F, 3.0F, -1.0F});
    ASSERT_NE(add_face(document, std::array{first, above, second, below}), kInvalidElementId);
    const Document source = document;
    Selection selection = qdr_test::vertex_selection({first, second});

    const auto result = connect_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    expect_one_connected_face(source, document, make_edge(first, second));
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    ASSERT_NE(find_face(document, concave_id), nullptr);
    EXPECT_EQ(find_face(document, concave_id)->vertices, concave_loop);
}

TEST(QdrDocumentOperationConnectVerticesTests, ConnectsSavedFoldedFaceAcrossAProjectedBoundaryCrossing) {
    Document document = saved_folded_connect_source();
    Document unaffected = document;
    std::erase_if(unaffected.faces, [](const Face& face) { return face.id == 5; });
    unaffected.next_vertex_id = Document{}.next_vertex_id;
    unaffected.next_face_id = Document{}.next_face_id;
    Selection selection = qdr_test::vertex_selection({7, 3});

    const auto result = connect_selected_vertices(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(document.faces.size(), 6U);
    EXPECT_EQ(document.next_vertex_id, 15U);
    EXPECT_EQ(document.next_face_id, 11U);
    expect_folded_connect_split(document);
    qdr_test::expect_unrelated_authored_geometry_preserved(document, unaffected);
    EXPECT_EQ(selection.mode, SelectionMode::Edge);
    EXPECT_EQ(selection.edges, (std::vector<Edge>{make_edge(3, 7)}));
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_edge, make_edge(3, 7));
}

TEST(QdrDocumentOperationConnectVerticesTests, FoldedConnectPreservesAttributesAcrossObliqueScaleAndWindingVariants) {
    for (const float scale : {0.125F, 1.0F, 8.0F}) {
        for (const bool reversed : {false, true}) {
            SCOPED_TRACE(::testing::Message() << "scale=" << scale << " reversed=" << reversed);
            Document document = saved_folded_connect_source();
            for (Vertex& vertex : document.vertices) {
                vertex.position = oblique_position(vertex.position, scale);
            }
            if (reversed) {
                for (Face& face : document.faces) {
                    std::ranges::reverse(face.vertices);
                }
            }
            Face& source_face = *find_face(document, 5);
            source_face.material_slot = 7;
            source_face.normal_shading = SurfaceShading::Smooth;
            source_face.texture_transform = {{-2.0F, 0.5F}, {0.125F, -0.25F}, 35.0F};
            source_face.uvs = {{0.25F, -0.5F}, {1.25F, 2.5F}, {-3.0F, 4.0F},
                {2.0F, 1.0F}, {3.0F, 2.0F}, {4.0F, 0.0F}};
            document.hard_normal_edges = {make_edge(2, 14)};
            document.soft_normal_edges = {make_edge(14, 7)};
            const Document source = document;
            const Face original_face = source_face;
            Selection selection = qdr_test::vertex_selection({7, 3});

            const auto result = connect_selected_vertices(document, selection);

            ASSERT_TRUE(result.success) << result.message;
            ASSERT_TRUE(result.changed);
            expect_folded_connect_split(document, reversed);
            EXPECT_EQ(document.hard_normal_edges, source.hard_normal_edges);
            EXPECT_EQ(document.soft_normal_edges, source.soft_normal_edges);
            for (const ElementId face_id : {ElementId{5}, ElementId{10}}) {
                const Face* replacement = find_face(document, face_id);
                ASSERT_NE(replacement, nullptr);
                EXPECT_EQ(replacement->material_slot, original_face.material_slot);
                EXPECT_EQ(replacement->normal_shading, original_face.normal_shading);
                EXPECT_EQ(replacement->texture_transform, original_face.texture_transform);
                ASSERT_EQ(replacement->uvs.size(), replacement->vertices.size());
                for (std::size_t corner = 0; corner < replacement->vertices.size(); ++corner) {
                    const auto original = std::ranges::find(original_face.vertices, replacement->vertices[corner]);
                    ASSERT_NE(original, original_face.vertices.end());
                    const auto uv = original_face.uvs[static_cast<std::size_t>(
                        std::distance(original_face.vertices.begin(), original))];
                    EXPECT_EQ(replacement->uvs[corner].x, uv.x);
                    EXPECT_EQ(replacement->uvs[corner].y, uv.y);
                }
            }
        }
    }
}

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

TEST(QdrDocumentOperationConnectVerticesTests, RejectsExteriorChordAcrossObliqueScaleAndMildWarpVariants) {
    const std::array positions{
        quader::QVec3{4.0F, 0.0F, 0.0F}, quader::QVec3{7.0F, 0.0F, 0.0F},
        quader::QVec3{7.0F, 3.0F, 0.0F}, quader::QVec3{6.0F, 3.0F, 0.0F},
        quader::QVec3{6.0F, 1.0F, 0.0F}, quader::QVec3{5.0F, 1.0F, 0.0F},
        quader::QVec3{5.0F, 3.0F, 0.0F}, quader::QVec3{4.0F, 3.0F, 0.0F}};
    for (const float scale : {0.125F, 1.0F, 8.0F}) {
        for (const bool warped : {false, true}) {
            SCOPED_TRACE(::testing::Message() << "scale=" << scale << " warped=" << warped);
            Document document;
            std::vector<ElementId> loop;
            for (std::size_t index = 0; index < positions.size(); ++index) {
                auto position = positions[index];
                if (warped && index == 4U) {
                    position.z = 0.125F;
                }
                loop.push_back(add_vertex(document, oblique_position(position, scale)));
            }
            ASSERT_NE(add_face(document, loop), kInvalidElementId);
            Selection selection = qdr_test::vertex_selection({loop[3], loop[6]});
            const auto source = qdr_test::document_fingerprint(document);
            const auto source_selection = qdr_test::selection_fingerprint(selection);

            const auto result = connect_selected_vertices(document, selection);

            EXPECT_FALSE(result.success);
            EXPECT_FALSE(result.changed);
            EXPECT_EQ(qdr_test::document_fingerprint(document), source);
            EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
        }
    }
}

TEST(QdrDocumentOperationConnectVerticesTests, RejectsCoincidentChordEndpointsAndZeroAreaFacesAtomically) {
    for (const bool coincident_endpoints : {true, false}) {
        SCOPED_TRACE(coincident_endpoints);
        auto fixture = qdr_test::make_quad_document();
        if (coincident_endpoints) {
            find_vertex(fixture.document, fixture.top_right)->position =
                find_vertex(fixture.document, fixture.bottom_left)->position;
        } else {
            for (std::size_t index = 0; index < fixture.document.vertices.size(); ++index) {
                fixture.document.vertices[index].position = {static_cast<float>(index), 0.0F, 0.0F};
            }
        }
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

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <algorithm>

using namespace quader_poly;

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveRetainsIdentityTransformAuthoredChartsAndNeighborCorners) {
    Document document = make_cube({});
    const Face source = document.faces.front();
    Selection split = qdr_test::edge_selection({
        make_edge(source.vertices[0], source.vertices[1]),
        make_edge(source.vertices[2], source.vertices[3])});
    ASSERT_TRUE(connect_selected_edges(document, split).changed);
    const Edge cut = split.edges.front();
    for (Face& face : document.faces) {
        face.uvs.clear();
        for (const ElementId id : face.vertices) {
            const auto position = find_vertex(document, id)->position;
            face.uvs.push_back({3.0F * position.x + position.z + 7.0F,
                2.0F * position.y - position.z - 5.0F});
        }
    }
    Selection selection = qdr_test::edge_selection({cut});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.changed) << result.message;
    for (const Face& face : document.faces) {
        ASSERT_EQ(face.uvs.size(), face.vertices.size());
        for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
            const auto position = find_vertex(document, face.vertices[corner])->position;
            EXPECT_NEAR(face.uvs[corner].x, 3.0F * position.x + position.z + 7.0F, 0.0001F);
            EXPECT_NEAR(face.uvs[corner].y, 2.0F * position.y - position.z - 5.0F, 0.0001F);
        }
    }
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveEdgesRemovesARedundantSupportCutAndRestoresOneQuad) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const FaceTextureTransform texture{{-2.0F, 0.5F}, {0.25F, 0.75F}, 25.0F};
	fixture.document.faces.front().texture_transform = texture;
	Selection connect_selection = qdr_test::edge_selection({
		make_edge(fixture.bottom_left, fixture.bottom_right),
		make_edge(fixture.top_right, fixture.top_left),
	});
	EXPECT_TRUE(connect_selected_edges(fixture.document, connect_selection).changed);
	qdr_test::expect_valid_polygon_document(fixture.document, "successful topology operation");
	EXPECT_TRUE(connect_selection.edges.size() == 1U);

	Selection dissolve_selection = qdr_test::edge_selection({ connect_selection.edges.front() }, connect_selection.edges.front());
	const OperationResult result = dissolve_selected_edges(fixture.document, dissolve_selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.vertices.size() == 4U);
	EXPECT_TRUE(fixture.document.faces.size() == 1U);
	EXPECT_TRUE(fixture.document.faces.front().vertices.size() == 4U);
	EXPECT_EQ(fixture.document.faces.front().texture_transform, texture);
	EXPECT_TRUE(fixture.document.faces.front().uvs.empty());
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveEdgesRestoresASharedCubeCutWithoutOpeningTheHull) {
	Document document = make_cube({});
	const Face face = document.faces.front();
	Selection connect_selection = qdr_test::edge_selection({
		make_edge(face.vertices[0], face.vertices[1]),
		make_edge(face.vertices[2], face.vertices[3]),
	});
	const std::size_t vertex_count_before = document.vertices.size();
	EXPECT_TRUE(connect_selected_edges(document, connect_selection).changed);
	qdr_test::expect_valid_polygon_document(document, "successful topology operation");
	const Edge cut_edge = connect_selection.edges.front();
	Selection dissolve_selection = qdr_test::edge_selection({ cut_edge }, cut_edge);

	const OperationResult result = dissolve_selected_edges(document, dissolve_selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before);
	EXPECT_TRUE(find_vertex(document, cut_edge.a) == nullptr);
	EXPECT_TRUE(find_vertex(document, cut_edge.b) == nullptr);
	EXPECT_FALSE(qdr_test::document_has_edge(document, cut_edge));
        EXPECT_TRUE(dissolve_selection.mode == SelectionMode::Face);
        EXPECT_TRUE(dissolve_selection.faces.size() == 1U);
	EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveProtectsOriginalTrianglePairCornersWithAUvBreakpoint) {
    Document document;
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId d = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const ElementId c = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId first = add_face(document, std::array{a, d, c});
    const ElementId second = add_face(document, std::array{d, b, c});
    find_face(document, first)->uvs = {{0.0F, 0.0F}, {0.1F, 0.0F}, {0.0F, 1.0F}};
    find_face(document, second)->uvs = {{0.1F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
    Selection selection = qdr_test::edge_selection({make_edge(d, c)});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    const Face& face = document.faces.front();
    ASSERT_EQ(face.vertices.size(), 4U);
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    const auto retained = std::ranges::find(face.vertices, d);
    ASSERT_NE(retained, face.vertices.end());
    const auto index = static_cast<std::size_t>(retained - face.vertices.begin());
    EXPECT_FLOAT_EQ(face.uvs[index].x, 0.1F);
    EXPECT_FLOAT_EQ(face.uvs[index].y, 0.0F);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveValidatesContinuousBilinearSubpatchesAfterBoundarySimplification) {
    Document document;
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId e = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const ElementId d = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId f = add_vertex(document, {1.0F, 2.0F, 0.0F});
    const ElementId c = add_vertex(document, {2.0F, 2.0F, 0.0F});
    const ElementId first = add_face(document, std::array{a, e, f, d});
    const ElementId second = add_face(document, std::array{e, b, c, f});
    find_face(document, first)->uvs = {{0.0F, 0.0F}, {0.5F, 0.0F}, {1.5F, 1.0F}, {0.0F, 1.0F}};
    find_face(document, second)->uvs = {{0.5F, 0.0F}, {1.0F, 0.0F}, {3.0F, 1.0F}, {1.5F, 1.0F}};
    Selection selection = qdr_test::edge_selection({make_edge(e, f)});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    const Face& face = document.faces.front();
    ASSERT_EQ(face.vertices.size(), 4U);
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    EXPECT_EQ(find_vertex(document, e), nullptr);
    EXPECT_EQ(find_vertex(document, f), nullptr);
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const auto position = find_vertex(document, face.vertices[index])->position;
        EXPECT_FLOAT_EQ(face.uvs[index].x, 0.5F * position.x * (1.0F + position.y));
        EXPECT_FLOAT_EQ(face.uvs[index].y, 0.5F * position.y);
    }
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveRemovesAnAuthoredSeamAndKeepsSurvivingBoundaryCorners) {
    Document document;
    const ElementId a = add_vertex(document, {-2.0F, -1.0F, 0.0F});
    const ElementId e = add_vertex(document, {0.0F, -2.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, -1.0F, 0.0F});
    const ElementId c = add_vertex(document, {2.0F, 1.0F, 0.0F});
    const ElementId f = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId d = add_vertex(document, {-2.0F, 1.0F, 0.0F});
    const ElementId first = add_face(document, std::array{a, e, f, d}, 7);
    const ElementId second = add_face(document, std::array{e, b, c, f}, 11);
    const FaceTextureTransform donor_texture{{0.125F, -0.25F}, {0.25F, -0.75F}, 30.0F};
    find_face(document, first)->texture_transform = donor_texture;
    find_face(document, first)->normal_shading = SurfaceShading::Smooth;
    find_face(document, second)->texture_transform = {{-0.5F, 0.75F}, {-1.0F, 0.5F}, -20.0F};
    const std::array<quader::QVec2, 4> first_uvs{{{0.0F, 0.0F}, {1.0F, -1.0F}, {1.0F, 2.0F}, {0.0F, 1.0F}}};
    const std::array<quader::QVec2, 4> second_uvs{{{3.0F, -1.0F}, {4.0F, 0.0F}, {4.0F, 1.0F}, {3.0F, 2.0F}}};
    for (std::size_t corner = 0; corner < first_uvs.size(); ++corner) {
        Face& first_face = *find_face(document, first);
        Face& second_face = *find_face(document, second);
        first_face.uvs.push_back(unapply_face_texture_transform(first_uvs[corner], first_face.texture_transform));
        second_face.uvs.push_back(unapply_face_texture_transform(second_uvs[corner], second_face.texture_transform));
    }
    const std::map<ElementId, quader::QVec2> expected_uvs{
        {a, first_uvs[0]}, {b, second_uvs[1]},
        {c, second_uvs[2]}, {d, first_uvs[3]}};
    const Edge seam = make_edge(e, f);
    Selection selection = qdr_test::edge_selection({seam}, seam);

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    ASSERT_EQ(document.vertices.size(), 4U);
    const Face& merged = document.faces.front();
    EXPECT_EQ(merged.id, first);
    EXPECT_EQ(merged.material_slot, 7U);
    EXPECT_EQ(merged.normal_shading, SurfaceShading::Smooth);
    EXPECT_EQ(merged.texture_transform, donor_texture);
    ASSERT_EQ(merged.vertices.size(), expected_uvs.size());
    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
    EXPECT_FALSE(qdr_test::document_has_edge(document, seam));
    EXPECT_EQ(find_vertex(document, e), nullptr);
    EXPECT_EQ(find_vertex(document, f), nullptr);
    EXPECT_EQ(find_face(document, second), nullptr);
    for (std::size_t corner = 0; corner < merged.vertices.size(); ++corner) {
        const auto expected = expected_uvs.at(merged.vertices[corner]);
        const auto actual = apply_face_texture_transform(merged.uvs[corner], merged.texture_transform);
        EXPECT_NEAR(actual.x, expected.x, 0.0001F);
        EXPECT_NEAR(actual.y, expected.y, 0.0001F);
    }
    EXPECT_EQ(selection.mode, SelectionMode::Face);
    EXPECT_EQ(selection.faces, std::vector<ElementId>{first});
    EXPECT_TRUE(selection.has_active);
    EXPECT_EQ(selection.active_kind, ElementKind::Face);
    EXPECT_EQ(selection.active_face, first);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveRemovesSupportVerticesDespiteAuthoredUvBreakpoints) {
    Document document;
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId e = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const ElementId d = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId f = add_vertex(document, {1.0F, 2.0F, 0.0F});
    const ElementId c = add_vertex(document, {2.0F, 2.0F, 0.0F});
    const ElementId first = add_face(document, std::array{a, e, f, d});
    const ElementId second = add_face(document, std::array{e, b, c, f});
    find_face(document, first)->uvs = {{0.0F, 0.0F}, {0.25F, 0.0F}, {1.75F, 2.0F}, {0.0F, 2.0F}};
    find_face(document, second)->uvs = {{0.25F, 0.0F}, {2.0F, 0.0F}, {2.0F, 2.0F}, {1.75F, 2.0F}};
    const std::map<ElementId, quader::QVec2> expected_uvs{
        {a, {0.0F, 0.0F}}, {b, {2.0F, 0.0F}},
        {c, {2.0F, 2.0F}}, {d, {0.0F, 2.0F}}};
    const Edge seam = make_edge(e, f);
    Selection selection = qdr_test::edge_selection({seam});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    ASSERT_EQ(document.vertices.size(), 4U);
    const Face& merged = document.faces.front();
    EXPECT_EQ(merged.id, first);
    ASSERT_EQ(merged.vertices.size(), expected_uvs.size());
    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
    EXPECT_FALSE(qdr_test::document_has_edge(document, seam));
    EXPECT_EQ(find_vertex(document, e), nullptr);
    EXPECT_EQ(find_vertex(document, f), nullptr);
    for (std::size_t corner = 0; corner < merged.vertices.size(); ++corner) {
        const auto expected = expected_uvs.at(merged.vertices[corner]);
        EXPECT_FLOAT_EQ(merged.uvs[corner].x, expected.x);
        EXPECT_FLOAT_EQ(merged.uvs[corner].y, expected.y);
    }
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveKeepsBoundaryUvsAcrossANoncoplanarQuadJoin) {
    Document document;
    const ElementId a = add_vertex(document, {-1.0F, 0.0F, 0.0F});
    const ElementId e = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {1.0F, 0.0F, 1.0F});
    const ElementId d = add_vertex(document, {-1.0F, 1.0F, 0.0F});
    const ElementId f = add_vertex(document, {0.0F, 1.0F, 0.0F});
    const ElementId c = add_vertex(document, {1.0F, 1.0F, 1.0F});
    const ElementId first = add_face(document, std::array{a, e, f, d});
    const ElementId second = add_face(document, std::array{e, b, c, f});
    find_face(document, first)->uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
    find_face(document, second)->uvs = {{1.0F, 0.0F}, {2.5F, 0.0F}, {2.5F, 1.0F}, {1.0F, 1.0F}};
    const std::map<ElementId, quader::QVec2> expected_uvs{
        {a, {0.0F, 0.0F}}, {b, {2.5F, 0.0F}},
        {c, {2.5F, 1.0F}}, {d, {0.0F, 1.0F}}};
    const auto original_vertices = document.vertices;
    const Edge join = make_edge(e, f);
    Selection selection = qdr_test::edge_selection({join});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    ASSERT_EQ(document.vertices.size(), 4U);
    const Face& merged = document.faces.front();
    EXPECT_EQ(merged.id, first);
    ASSERT_EQ(merged.vertices.size(), expected_uvs.size());
    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
    EXPECT_FALSE(qdr_test::document_has_edge(document, join));
    EXPECT_EQ(find_vertex(document, e), nullptr);
    EXPECT_EQ(find_vertex(document, f), nullptr);
    for (std::size_t corner = 0; corner < merged.vertices.size(); ++corner) {
        const auto expected = expected_uvs.at(merged.vertices[corner]);
        EXPECT_FLOAT_EQ(merged.uvs[corner].x, expected.x);
        EXPECT_FLOAT_EQ(merged.uvs[corner].y, expected.y);
    }
    for (const Vertex& original : original_vertices) {
        if (original.id == e || original.id == f) {
            continue;
        }
        const Vertex* retained = find_vertex(document, original.id);
        ASSERT_NE(retained, nullptr);
        EXPECT_FLOAT_EQ(retained->position.x, original.position.x);
        EXPECT_FLOAT_EQ(retained->position.y, original.position.y);
        EXPECT_FLOAT_EQ(retained->position.z, original.position.z);
    }
    EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveCompletesTheSelectedBatchBeforeCleaningItsVertices) {
    Document source;
    const ElementId center = add_vertex(source, {0.0F, 0.0F, 0.0F});
    const ElementId right = add_vertex(source, {1.0F, 0.0F, 0.0F});
    const ElementId upper_left = add_vertex(source, {-0.5F, 0.8660254F, 0.0F});
    const ElementId lower_left = add_vertex(source, {-0.5F, -0.8660254F, 0.0F});
    const ElementId upper = add_vertex(source, {1.0F, 1.7320508F, 0.0F});
    const ElementId left = add_vertex(source, {-2.0F, 0.0F, 0.0F});
    const ElementId lower = add_vertex(source, {1.0F, -1.7320508F, 0.0F});
    const ElementId first = add_face(source, std::array{center, right, upper, upper_left});
    static_cast<void>(add_face(source, std::array{center, upper_left, left, lower_left}));
    static_cast<void>(add_face(source, std::array{center, lower_left, lower, right}));
    for (Face& face : source.faces) {
        for (const ElementId vertex_id : face.vertices) {
            const auto position = find_vertex(source, vertex_id)->position;
            face.uvs.push_back({position.x + 3.0F, position.y - 4.0F});
        }
    }
    source.hard_normal_edges = {make_edge(center, right), make_edge(right, upper),
        make_edge(upper_left, left), make_edge(left, lower_left)};
    const std::array selected_edges{make_edge(center, right), make_edge(center, upper_left)};
    const std::array expected_hard_edges{make_edge(lower, upper), make_edge(upper, left),
        make_edge(left, lower_left)};

    for (const bool reverse_order : {false, true}) {
        SCOPED_TRACE(reverse_order ? "reversed selected edges" : "original selected edges");
        Document document = source;
        Selection selection = qdr_test::edge_selection({selected_edges[0], selected_edges[1]});
        if (reverse_order) {
            std::ranges::reverse(selection.edges);
        }

        const OperationResult result = dissolve_selected_edges(document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(document.faces.size(), 1U);
        ASSERT_EQ(document.vertices.size(), 4U);
        const Face& merged = document.faces.front();
        EXPECT_EQ(merged.id, first);
        ASSERT_EQ(merged.vertices.size(), 4U);
        ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
        for (const ElementId removed : {center, right, upper_left}) {
            EXPECT_EQ(find_vertex(document, removed), nullptr);
        }
        // The unselected rim midpoint also ends at degree two, but is outside cleanup.
        EXPECT_NE(find_vertex(document, lower_left), nullptr);
        for (std::size_t corner = 0; corner < merged.vertices.size(); ++corner) {
            const auto position = find_vertex(document, merged.vertices[corner])->position;
            EXPECT_FLOAT_EQ(merged.uvs[corner].x, position.x + 3.0F);
            EXPECT_FLOAT_EQ(merged.uvs[corner].y, position.y - 4.0F);
        }
        ASSERT_EQ(document.hard_normal_edges.size(), expected_hard_edges.size());
        for (const Edge edge : expected_hard_edges) {
            EXPECT_NE(std::ranges::find(document.hard_normal_edges, edge), document.hard_normal_edges.end());
            EXPECT_TRUE(qdr_test::document_has_edge(document, edge));
        }
        EXPECT_EQ(selection.mode, SelectionMode::Face);
        EXPECT_EQ(selection.faces, std::vector<ElementId>{first});
        EXPECT_EQ(selection.active_face, first);
        EXPECT_TRUE(validate_polygon_selection_liveness(document, selection).ok());
        qdr_test::expect_valid_polygon_document_after_success(document, result);
    }
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveKeepsAJunctionUsedByAnUnselectedFace) {
    Document document;
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId e = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const ElementId d = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId f = add_vertex(document, {1.0F, 2.0F, 0.0F});
    const ElementId c = add_vertex(document, {2.0F, 2.0F, 0.0F});
    const ElementId below = add_vertex(document, {0.5F, -1.0F, 0.0F});
    const ElementId first = add_face(document, std::array{a, e, f, d});
    static_cast<void>(add_face(document, std::array{e, b, c, f}));
    const ElementId neighbor = add_face(document, std::array{a, below, e}, 9);
    for (Face& face : document.faces) {
        for (const ElementId vertex_id : face.vertices) {
            const auto position = find_vertex(document, vertex_id)->position;
            face.uvs.push_back({position.x + 3.0F, position.y - 4.0F});
        }
    }
    const Face original_neighbor = *find_face(document, neighbor);
    Selection selection = qdr_test::edge_selection({make_edge(e, f)});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 2U);
    ASSERT_EQ(document.vertices.size(), 6U);
    EXPECT_EQ(find_vertex(document, f), nullptr);
    EXPECT_NE(find_vertex(document, e), nullptr);
    const Face* merged = find_face(document, first);
    ASSERT_NE(merged, nullptr);
    ASSERT_EQ(merged->vertices.size(), 5U);
    EXPECT_NE(std::ranges::find(merged->vertices, e), merged->vertices.end());
    const Face* retained_neighbor = find_face(document, neighbor);
    ASSERT_NE(retained_neighbor, nullptr);
    EXPECT_EQ(retained_neighbor->vertices, original_neighbor.vertices);
    EXPECT_EQ(retained_neighbor->material_slot, original_neighbor.material_slot);
    ASSERT_EQ(retained_neighbor->uvs.size(), original_neighbor.uvs.size());
    for (std::size_t corner = 0; corner < retained_neighbor->uvs.size(); ++corner) {
        EXPECT_FLOAT_EQ(retained_neighbor->uvs[corner].x, original_neighbor.uvs[corner].x);
        EXPECT_FLOAT_EQ(retained_neighbor->uvs[corner].y, original_neighbor.uvs[corner].y);
    }
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveProtectsBothCornersOfAnOriginalTrianglePairDiagonal) {
    Document document;
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const ElementId c = add_vertex(document, {2.0F, 2.0F, 0.0F});
    const ElementId d = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId first = add_face(document, std::array{a, b, c});
    static_cast<void>(add_face(document, std::array{a, c, d}));
    Selection selection = qdr_test::edge_selection({make_edge(a, c)});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    ASSERT_EQ(document.vertices.size(), 4U);
    const Face& merged = document.faces.front();
    EXPECT_EQ(merged.id, first);
    ASSERT_EQ(merged.vertices.size(), 4U);
    for (const ElementId corner : {a, b, c, d}) {
        EXPECT_NE(std::ranges::find(merged.vertices, corner), merged.vertices.end());
    }
    EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(a, c)));
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

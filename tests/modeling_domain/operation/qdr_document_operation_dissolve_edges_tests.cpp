////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <algorithm>

using namespace quader_poly;

namespace {

/// Retains a removable quad seam and both support-corner identities.
struct DissolvableSeamFixture {
    Document document;
    Edge seam;
    ElementId first_face = kInvalidElementId;
};

DissolvableSeamFixture make_dissolvable_seam_fixture()
{
    DissolvableSeamFixture fixture;
    const ElementId left_bottom = add_vertex(fixture.document, {0.0F, 0.0F, 0.0F});
    const ElementId middle_bottom = add_vertex(fixture.document, {1.0F, 0.0F, 0.0F});
    const ElementId right_bottom = add_vertex(fixture.document, {2.0F, 0.0F, 0.0F});
    const ElementId left_top = add_vertex(fixture.document, {0.0F, 2.0F, 0.0F});
    const ElementId middle_top = add_vertex(fixture.document, {1.0F, 2.0F, 0.0F});
    const ElementId right_top = add_vertex(fixture.document, {2.0F, 2.0F, 0.0F});
    fixture.first_face = add_face(fixture.document,
        std::array{left_bottom, middle_bottom, middle_top, left_top}, 7);
    const ElementId second_face = add_face(fixture.document,
        std::array{middle_bottom, right_bottom, right_top, middle_top}, 7);
    find_face(fixture.document, fixture.first_face)->normal_shading = SurfaceShading::Smooth;
    find_face(fixture.document, second_face)->normal_shading = SurfaceShading::Smooth;
    for (Face& face : fixture.document.faces) {
        for (const ElementId vertex_id : face.vertices) {
            const auto position = find_vertex(fixture.document, vertex_id)->position;
            face.uvs.push_back({position.x + 3.0F, position.y - 4.0F});
        }
    }
    fixture.seam = make_edge(middle_bottom, middle_top);
    return fixture;
}

/// Splits one rectangular face with a seam containing two authored degree-two vertices.
struct SubdividedSeamFixture {
    Document document;
    std::array<ElementId, 4> boundary;
    std::array<ElementId, 4> seam;
    ElementId first_face = kInvalidElementId;
};

SubdividedSeamFixture make_subdivided_seam_fixture(bool relabeled, bool reversed)
{
    SubdividedSeamFixture fixture;
    const std::array<quader::QVec3, 8> positions{{
        {-1.0F, -1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
        {1.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 0.0F},
        {0.0F, 1.0F, 0.0F}, {-1.0F, 1.0F, 0.0F},
        {0.0F, -0.25F, 0.0F}, {0.0F, 0.25F, 0.0F}}};
    std::array<ElementId, 8> vertices{};
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const std::size_t slot = relabeled ? vertices.size() - index - 1U : index;
        const auto position = positions[slot];
        vertices[slot] = add_vertex(fixture.document, relabeled
            ? quader::QVec3{position.y + 10.0F, 3.0F, -position.x - 2.0F}
            : position);
    }
    fixture.boundary = {vertices[0], vertices[2], vertices[3], vertices[5]};
    fixture.seam = {vertices[1], vertices[6], vertices[7], vertices[4]};
    fixture.first_face = add_face(fixture.document,
        std::array{vertices[0], vertices[1], vertices[6], vertices[7], vertices[4], vertices[5]}, 7U);
    static_cast<void>(add_face(fixture.document,
        std::array{vertices[1], vertices[2], vertices[3], vertices[4], vertices[7], vertices[6]}, 11U));
    for (Face& face : fixture.document.faces) {
        if (reversed) {
            std::ranges::reverse(face.vertices);
        }
        face.normal_shading = SurfaceShading::Smooth;
        for (const ElementId id : face.vertices) {
            const auto position = find_vertex(fixture.document, id)->position;
            face.uvs.push_back({position.x + 3.0F, position.y + position.z - 4.0F});
        }
    }
    if (reversed) {
        std::ranges::reverse(fixture.boundary);
    }
    return fixture;
}

} // namespace

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

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveKeepsBentBoundaryCornerAndRemovesStraightOppositeCorner) {
    DissolvableSeamFixture fixture = make_dissolvable_seam_fixture();
    Vertex* bent = find_vertex(fixture.document, fixture.seam.a);
    ASSERT_NE(bent, nullptr);
    bent->position.y = -0.5F;
    const quader::QVec3 original_position = bent->position;
    const Face original_face = *find_face(fixture.document, fixture.first_face);
    const auto original_corner = std::ranges::find(original_face.vertices, bent->id);
    ASSERT_NE(original_corner, original_face.vertices.end());
    const quader::QVec2 original_uv = original_face.uvs[
        static_cast<std::size_t>(original_corner - original_face.vertices.begin())];
    const auto original_edges = document_edges(fixture.document);
    std::vector<Edge> retained_edges;
    for (const Edge edge : original_edges) {
        if ((edge.a == bent->id || edge.b == bent->id) && edge != fixture.seam) {
            retained_edges.push_back(edge);
        }
    }
    ASSERT_EQ(retained_edges.size(), 2U);
    fixture.document.hard_normal_edges = {retained_edges[0]};
    fixture.document.soft_normal_edges = {retained_edges[1]};
    Selection selection = qdr_test::edge_selection({fixture.seam}, fixture.seam);

    const OperationResult result = dissolve_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(fixture.document.faces.size(), 1U);
    ASSERT_EQ(fixture.document.vertices.size(), 5U);
    const Vertex* retained = find_vertex(fixture.document, fixture.seam.a);
    ASSERT_NE(retained, nullptr);
    EXPECT_FLOAT_EQ(retained->position.x, original_position.x);
    EXPECT_FLOAT_EQ(retained->position.y, original_position.y);
    EXPECT_FLOAT_EQ(retained->position.z, original_position.z);
    EXPECT_EQ(find_vertex(fixture.document, fixture.seam.b), nullptr);
    const Face& merged = fixture.document.faces.front();
    EXPECT_EQ(merged.id, fixture.first_face);
    EXPECT_EQ(merged.material_slot, original_face.material_slot);
    EXPECT_EQ(merged.normal_shading, original_face.normal_shading);
    ASSERT_EQ(merged.vertices.size(), 5U);
    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
    const auto retained_corner = std::ranges::find(merged.vertices, retained->id);
    ASSERT_NE(retained_corner, merged.vertices.end());
    const std::size_t corner =
        static_cast<std::size_t>(retained_corner - merged.vertices.begin());
    EXPECT_FLOAT_EQ(merged.uvs[corner].x, original_uv.x);
    EXPECT_FLOAT_EQ(merged.uvs[corner].y, original_uv.y);
    EXPECT_TRUE(qdr_test::contains_edge(fixture.document.hard_normal_edges, retained_edges[0]));
    EXPECT_TRUE(qdr_test::contains_edge(fixture.document.soft_normal_edges, retained_edges[1]));
    EXPECT_FALSE(qdr_test::document_has_edge(fixture.document, fixture.seam));
    EXPECT_EQ(selection.faces, std::vector<ElementId>{fixture.first_face});
    EXPECT_TRUE(validate_polygon_selection_liveness(fixture.document, selection).ok());
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveUsesFiveDegreeBoundaryStraightnessThreshold) {
    for (const auto [offset, keep_corner] :
         std::array{std::pair{0.03F, false}, std::pair{0.08F, true}}) {
        SCOPED_TRACE(offset);
        DissolvableSeamFixture fixture = make_dissolvable_seam_fixture();
        find_vertex(fixture.document, fixture.seam.a)->position.y = -offset;
        Selection selection = qdr_test::edge_selection({fixture.seam});

        const OperationResult result = dissolve_selected_edges(fixture.document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        EXPECT_EQ(find_vertex(fixture.document, fixture.seam.a) != nullptr, keep_corner);
        EXPECT_EQ(find_vertex(fixture.document, fixture.seam.b), nullptr);
        ASSERT_EQ(fixture.document.faces.size(), 1U);
        EXPECT_EQ(fixture.document.faces.front().vertices.size(), keep_corner ? 5U : 4U);
        qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
    }
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
        {a, first_uvs[0]}, {e, second_uvs[0]}, {b, second_uvs[1]},
        {c, second_uvs[2]}, {f, first_uvs[2]}, {d, first_uvs[3]}};
    const Edge seam = make_edge(e, f);
    Selection selection = qdr_test::edge_selection({seam}, seam);

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    ASSERT_EQ(document.vertices.size(), 6U);
    const Face& merged = document.faces.front();
    EXPECT_EQ(merged.id, first);
    EXPECT_EQ(merged.material_slot, 7U);
    EXPECT_EQ(merged.normal_shading, SurfaceShading::Smooth);
    EXPECT_EQ(merged.texture_transform, donor_texture);
    ASSERT_EQ(merged.vertices.size(), expected_uvs.size());
    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
    EXPECT_FALSE(qdr_test::document_has_edge(document, seam));
    EXPECT_NE(find_vertex(document, e), nullptr);
    EXPECT_NE(find_vertex(document, f), nullptr);
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

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveSupportVertexUsesTheLastOrderedNormalModeDonor) {
    Document document;
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId e = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const ElementId d = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId f = add_vertex(document, {1.0F, 2.0F, 0.0F});
    const ElementId c = add_vertex(document, {2.0F, 2.0F, 0.0F});
    ASSERT_NE(add_face(document, std::array{a, e, f, d}),
              kInvalidElementId);
    ASSERT_NE(add_face(document, std::array{e, b, c, f}),
              kInvalidElementId);
    document.hard_normal_edges = {make_edge(a, e)};
    document.soft_normal_edges = {make_edge(e, b)};
    Selection selection = qdr_test::edge_selection({make_edge(e, f)});

    const OperationResult result =
        dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    const Edge replacement = make_edge(a, b);
    EXPECT_TRUE(qdr_test::document_has_edge(document, replacement));
    EXPECT_TRUE(qdr_test::contains_edge(document.soft_normal_edges,
                                        replacement));
    EXPECT_FALSE(qdr_test::contains_edge(document.hard_normal_edges,
                                         replacement));
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
        {a, {0.0F, 0.0F}}, {e, {1.0F, 0.0F}}, {b, {2.5F, 0.0F}},
        {c, {2.5F, 1.0F}}, {f, {1.0F, 1.0F}}, {d, {0.0F, 1.0F}}};
    const auto original_vertices = document.vertices;
    const Edge join = make_edge(e, f);
    Selection selection = qdr_test::edge_selection({join});

    const OperationResult result = dissolve_selected_edges(document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(document.faces.size(), 1U);
    ASSERT_EQ(document.vertices.size(), 6U);
    const Face& merged = document.faces.front();
    EXPECT_EQ(merged.id, first);
    ASSERT_EQ(merged.vertices.size(), expected_uvs.size());
    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
    EXPECT_FALSE(qdr_test::document_has_edge(document, join));
    EXPECT_NE(find_vertex(document, e), nullptr);
    EXPECT_NE(find_vertex(document, f), nullptr);
    for (std::size_t corner = 0; corner < merged.vertices.size(); ++corner) {
        const auto expected = expected_uvs.at(merged.vertices[corner]);
        EXPECT_FLOAT_EQ(merged.uvs[corner].x, expected.x);
        EXPECT_FLOAT_EQ(merged.uvs[corner].y, expected.y);
    }
    for (const Vertex& original : original_vertices) {
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
    const Edge expected_default_edge = make_edge(lower, upper);
    const std::array expected_hard_edges{make_edge(upper, left),
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
        EXPECT_TRUE(qdr_test::document_has_edge(document, expected_default_edge));
        EXPECT_FALSE(qdr_test::contains_edge(
            document.hard_normal_edges, expected_default_edge));
        EXPECT_FALSE(qdr_test::contains_edge(
            document.soft_normal_edges, expected_default_edge));
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

TEST(QdrDocumentOperationDissolveEdgesTests, DissolvePreservesUnrelatedCollapsedFacesAndLooseVertices) {
    DissolvableSeamFixture fixture = make_dissolvable_seam_fixture();
    const Document island = qdr_test::append_unrelated_authored_geometry(fixture.document);
    Selection selection = qdr_test::edge_selection({fixture.seam}, fixture.seam);

    const OperationResult result = dissolve_selected_edges(fixture.document, selection);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    EXPECT_FALSE(qdr_test::document_has_edge(fixture.document, fixture.seam));
    EXPECT_EQ(find_vertex(fixture.document, fixture.seam.a), nullptr);
    EXPECT_EQ(find_vertex(fixture.document, fixture.seam.b), nullptr);
    ASSERT_EQ(fixture.document.faces.size(), 2U);
    const Face* merged = find_face(fixture.document, fixture.first_face);
    ASSERT_NE(merged, nullptr);
    ASSERT_EQ(merged->vertices.size(), 4U);
    ASSERT_EQ(merged->uvs.size(), merged->vertices.size());
    EXPECT_EQ(merged->material_slot, 7U);
    EXPECT_EQ(merged->normal_shading, SurfaceShading::Smooth);
    for (std::size_t corner = 0; corner < merged->vertices.size(); ++corner) {
        const auto position = find_vertex(fixture.document, merged->vertices[corner])->position;
        EXPECT_FLOAT_EQ(merged->uvs[corner].x, position.x + 3.0F);
        EXPECT_FLOAT_EQ(merged->uvs[corner].y, position.y - 4.0F);
    }
    EXPECT_TRUE(qdr_test::face_compiles_to_triangles(fixture.document, *merged));
    EXPECT_EQ(selection.mode, SelectionMode::Face);
    EXPECT_EQ(selection.faces, std::vector<ElementId>{fixture.first_face});
    EXPECT_TRUE(validate_polygon_selection_liveness(fixture.document, selection).ok());
    qdr_test::expect_unrelated_authored_geometry_preserved(fixture.document, island);
}

TEST(QdrDocumentOperationDissolveEdgesTests, DissolveRejectsNonfiniteOrMalformedInputAtomically) {
    for (const auto fault : qdr_test::kAuthoredInputFaults) {
        SCOPED_TRACE(static_cast<int>(fault));
        DissolvableSeamFixture fixture = make_dissolvable_seam_fixture();
        static_cast<void>(qdr_test::append_unrelated_authored_geometry(fixture.document));
        qdr_test::corrupt_unrelated_authored_geometry(fixture.document, fault);
        Selection selection = qdr_test::edge_selection({fixture.seam}, fixture.seam);
        const std::string original_document = qdr_test::document_fingerprint(fixture.document);
        const std::string original_selection = qdr_test::selection_fingerprint(selection);

        const OperationResult result = dissolve_selected_edges(fixture.document, selection);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(fixture.document), original_document);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), original_selection);
    }
}

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveRemovesTheWholeSubdividedSeamFromAnySelectedSegment) {
    for (const bool relabeled : {false, true}) {
        for (const bool reversed : {false, true}) {
            const auto fixture = make_subdivided_seam_fixture(relabeled, reversed);
            for (const int selected_segment : {-1, 0, 1, 2}) {
                std::string expected_document;
                for (const bool reverse_selection : {false, true}) {
                    SCOPED_TRACE(::testing::Message()
                        << "relabeled=" << relabeled << ", reversed=" << reversed
                        << ", segment=" << selected_segment
                        << ", reverse selection=" << reverse_selection);
                    Document document = fixture.document;
                    std::vector<Edge> edges;
                    for (std::size_t index = 0; index + 1U < fixture.seam.size(); ++index) {
                        if (selected_segment < 0 || index == static_cast<std::size_t>(selected_segment)) {
                            edges.push_back(make_edge(fixture.seam[index], fixture.seam[index + 1U]));
                        }
                    }
                    Selection selection = qdr_test::edge_selection({edges.front()}, edges.front());
                    selection.edges = edges;
                    if (reverse_selection) {
                        std::ranges::reverse(selection.edges);
                        selection.edges.push_back(selection.edges.front());
                        std::swap(selection.edges.back().a, selection.edges.back().b);
                    }

                    const OperationResult result = dissolve_selected_edges(document, selection);

                    ASSERT_TRUE(result.success) << result.message;
                    ASSERT_TRUE(result.changed);
                    ASSERT_EQ(document.faces.size(), 1U);
                    ASSERT_EQ(document.vertices.size(), fixture.boundary.size());
                    const Face& merged = document.faces.front();
                    EXPECT_EQ(merged.id, fixture.first_face);
                    EXPECT_EQ(merged.material_slot, 7U);
                    EXPECT_EQ(merged.normal_shading, SurfaceShading::Smooth);
                    ASSERT_EQ(merged.vertices.size(), fixture.boundary.size());
                    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
                    auto boundary = merged.vertices;
                    const auto start = std::ranges::find(boundary, fixture.boundary.front());
                    ASSERT_NE(start, boundary.end());
                    std::rotate(boundary.begin(), start, boundary.end());
                    EXPECT_EQ(boundary, (std::vector<ElementId>{
                        fixture.boundary.begin(), fixture.boundary.end()}));
                    for (const ElementId id : fixture.seam) {
                        EXPECT_EQ(find_vertex(document, id), nullptr);
                    }
                    for (std::size_t corner = 0; corner < merged.vertices.size(); ++corner) {
                        const Vertex* original = find_vertex(fixture.document, merged.vertices[corner]);
                        const Vertex* retained = find_vertex(document, merged.vertices[corner]);
                        ASSERT_NE(original, nullptr);
                        ASSERT_NE(retained, nullptr);
                        EXPECT_FLOAT_EQ(retained->position.x, original->position.x);
                        EXPECT_FLOAT_EQ(retained->position.y, original->position.y);
                        EXPECT_FLOAT_EQ(retained->position.z, original->position.z);
                        EXPECT_FLOAT_EQ(merged.uvs[corner].x, original->position.x + 3.0F);
                        EXPECT_FLOAT_EQ(merged.uvs[corner].y,
                            original->position.y + original->position.z - 4.0F);
                    }
                    EXPECT_EQ(selection.mode, SelectionMode::Face);
                    EXPECT_EQ(selection.faces, std::vector<ElementId>{fixture.first_face});
                    EXPECT_TRUE(selection.has_active);
                    EXPECT_EQ(selection.active_kind, ElementKind::Face);
                    EXPECT_EQ(selection.active_face, fixture.first_face);
                    EXPECT_TRUE(selection.edges.empty());
                    EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
                    qdr_test::expect_valid_polygon_document_after_success(document, result);
                    if (expected_document.empty()) {
                        expected_document = qdr_test::document_fingerprint(document);
                    } else {
                        EXPECT_EQ(qdr_test::document_fingerprint(document), expected_document);
                    }
                }
            }
        }
    }
}

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveKeepsAStraightMultiSeamJunctionTouchedByAnUnselectedFace) {
    Document source;
    const ElementId junction = add_vertex(source, {0.0F, 0.0F, 0.0F});
    const ElementId left = add_vertex(source, {-1.0F, 0.0F, 0.0F});
    const ElementId upper_left = add_vertex(source, {-0.4F, 1.0F, 0.0F});
    const ElementId upper_right = add_vertex(source, {0.4F, 1.0F, 0.0F});
    const ElementId right = add_vertex(source, {1.0F, 0.0F, 0.0F});
    const ElementId below = add_vertex(source, {0.0F, -1.0F, 0.0F});
    const ElementId first = add_face(source, std::array{junction, left, upper_left}, 7U);
    ASSERT_NE(first, kInvalidElementId);
    ASSERT_NE(add_face(source, std::array{junction, upper_left, upper_right}, 7U), kInvalidElementId);
    ASSERT_NE(add_face(source, std::array{junction, upper_right, right}, 7U), kInvalidElementId);
    const ElementId neighbor = add_face(source, std::array{junction, right, below, left}, 11U);
    ASSERT_NE(neighbor, kInvalidElementId);
    for (Face& face : source.faces) {
        for (const ElementId id : face.vertices) {
            const auto position = find_vertex(source, id)->position;
            face.uvs.push_back({position.x + 3.0F, position.y - 4.0F});
        }
    }
    const Face original_neighbor = *find_face(source, neighbor);
    std::string expected_document;
    for (const bool reversed : {false, true}) {
        Document document = source;
        Selection selection = qdr_test::edge_selection({
            make_edge(junction, upper_left), make_edge(junction, upper_right)});
        if (reversed) {
            std::ranges::reverse(selection.edges);
        }

        const OperationResult result = dissolve_selected_edges(document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(document.faces.size(), 2U);
        ASSERT_EQ(document.vertices.size(), source.vertices.size());
        const Face* merged = find_face(document, first);
        ASSERT_NE(merged, nullptr);
        ASSERT_EQ(merged->vertices.size(), 5U);
        auto boundary = merged->vertices;
        const auto start = std::ranges::find(boundary, junction);
        ASSERT_NE(start, boundary.end());
        std::rotate(boundary.begin(), start, boundary.end());
        EXPECT_EQ(boundary, (std::vector<ElementId>{junction, left, upper_left, upper_right, right}));
        EXPECT_EQ(merged->material_slot, 7U);
        const Face* retained_neighbor = find_face(document, neighbor);
        ASSERT_NE(retained_neighbor, nullptr);
        EXPECT_EQ(retained_neighbor->vertices, original_neighbor.vertices);
        EXPECT_EQ(retained_neighbor->material_slot, original_neighbor.material_slot);
        EXPECT_EQ(retained_neighbor->texture_transform, original_neighbor.texture_transform);
        for (const Face& face : document.faces) {
            ASSERT_EQ(face.uvs.size(), face.vertices.size());
            for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
                const auto position = find_vertex(document, face.vertices[corner])->position;
                EXPECT_FLOAT_EQ(face.uvs[corner].x, position.x + 3.0F);
                EXPECT_FLOAT_EQ(face.uvs[corner].y, position.y - 4.0F);
            }
        }
        EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(junction, upper_left)));
        EXPECT_FALSE(qdr_test::document_has_edge(document, make_edge(junction, upper_right)));
        EXPECT_EQ(selection.mode, SelectionMode::Face);
        EXPECT_EQ(selection.faces, std::vector<ElementId>{first});
        EXPECT_EQ(selection.active_face, first);
        EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        if (reversed) {
            EXPECT_EQ(qdr_test::document_fingerprint(document), expected_document);
        } else {
            expected_document = qdr_test::document_fingerprint(document);
        }
    }
}

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveRejectsAClosedDegreeTwoSeamWithoutChangingAuthoredState) {
    auto fixture = qdr_test::make_quad_document(7U);
    auto opposite = fixture.document.faces.front().vertices;
    std::ranges::reverse(opposite);
    ASSERT_NE(add_face(fixture.document, opposite, 11U), kInvalidElementId);
    for (Face& face : fixture.document.faces) {
        for (const ElementId id : face.vertices) {
            const auto position = find_vertex(fixture.document, id)->position;
            face.uvs.push_back({position.x + 3.0F, position.y - 4.0F});
        }
    }
    const auto edges = document_edges(fixture.document);
    for (const bool whole_loop : {false, true}) {
        Document document = fixture.document;
        Selection selection = qdr_test::edge_selection({edges.front()}, edges.front());
        if (whole_loop) {
            selection.edges = edges;
        }
        const auto original_document = qdr_test::document_fingerprint(document);
        const auto original_selection = qdr_test::selection_fingerprint(selection);

        const OperationResult result = dissolve_selected_edges(document, selection);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(document), original_document);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), original_selection);
    }
}

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveRejectsAMixedInteriorAndBoundarySelectionAtomically) {
    const auto fixture = make_dissolvable_seam_fixture();
    const auto& face = fixture.document.faces.front();
    const Edge boundary = make_edge(face.vertices[0], face.vertices[1]);
    for (const bool reversed : {false, true}) {
        Document document = fixture.document;
        Selection selection = qdr_test::edge_selection({fixture.seam, boundary}, fixture.seam);
        if (reversed) {
            std::ranges::reverse(selection.edges);
        }
        const auto original_document = qdr_test::document_fingerprint(document);
        const auto original_selection = qdr_test::selection_fingerprint(selection);

        const OperationResult result = dissolve_selected_edges(document, selection);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(qdr_test::document_fingerprint(document), original_document);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), original_selection);
    }
}

TEST(QdrDocumentOperationDissolveEdgesTests,
     DissolveProcessesDisconnectedSeamsWithoutConnectingTheirBoundaries) {
    Document source;
    std::array<ElementId, 2> surviving_faces{};
    std::array<Edge, 2> seams{};
    std::array<std::array<ElementId, 4>, 2> boundaries{};
    for (std::size_t island = 0; island < seams.size(); ++island) {
        const float offset = 5.0F * static_cast<float>(island);
        const ElementId left_bottom = add_vertex(source, {offset, 0.0F, 0.0F});
        const ElementId middle_bottom = add_vertex(source, {offset + 1.0F, 0.0F, 0.0F});
        const ElementId right_bottom = add_vertex(source, {offset + 2.0F, 0.0F, 0.0F});
        const ElementId left_top = add_vertex(source, {offset, 2.0F, 0.0F});
        const ElementId middle_top = add_vertex(source, {offset + 1.0F, 2.0F, 0.0F});
        const ElementId right_top = add_vertex(source, {offset + 2.0F, 2.0F, 0.0F});
        surviving_faces[island] = add_face(source,
            std::array{left_bottom, middle_bottom, middle_top, left_top}, 7U);
        ASSERT_NE(surviving_faces[island], kInvalidElementId);
        ASSERT_NE(add_face(source,
            std::array{middle_bottom, right_bottom, right_top, middle_top}, 7U), kInvalidElementId);
        seams[island] = make_edge(middle_bottom, middle_top);
        boundaries[island] = {left_bottom, right_bottom, right_top, left_top};
    }
    for (Face& face : source.faces) {
        for (const ElementId id : face.vertices) {
            const auto position = find_vertex(source, id)->position;
            face.uvs.push_back({position.x + 3.0F, position.y - 4.0F});
        }
    }
    std::string expected_document;
    for (const bool reversed : {false, true}) {
        Document document = source;
        Selection selection = qdr_test::edge_selection({seams[0], seams[1]}, seams[0]);
        if (reversed) {
            std::ranges::reverse(selection.edges);
        }

        const OperationResult result = dissolve_selected_edges(document, selection);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        ASSERT_EQ(document.faces.size(), 2U);
        ASSERT_EQ(document.vertices.size(), 8U);
        for (std::size_t island = 0; island < seams.size(); ++island) {
            const Face* merged = find_face(document, surviving_faces[island]);
            ASSERT_NE(merged, nullptr);
            auto boundary = merged->vertices;
            const auto start = std::ranges::find(boundary, boundaries[island].front());
            ASSERT_NE(start, boundary.end());
            std::rotate(boundary.begin(), start, boundary.end());
            EXPECT_EQ(boundary, (std::vector<ElementId>{boundaries[island].begin(), boundaries[island].end()}));
            ASSERT_EQ(merged->uvs.size(), merged->vertices.size());
            for (std::size_t corner = 0; corner < merged->vertices.size(); ++corner) {
                const auto position = find_vertex(document, merged->vertices[corner])->position;
                EXPECT_FLOAT_EQ(merged->uvs[corner].x, position.x + 3.0F);
                EXPECT_FLOAT_EQ(merged->uvs[corner].y, position.y - 4.0F);
            }
            EXPECT_EQ(find_vertex(document, seams[island].a), nullptr);
            EXPECT_EQ(find_vertex(document, seams[island].b), nullptr);
        }
        auto selected_faces = selection.faces;
        std::ranges::sort(selected_faces);
        EXPECT_EQ(selected_faces, (std::vector<ElementId>{surviving_faces.begin(), surviving_faces.end()}));
        EXPECT_EQ(selection.mode, SelectionMode::Face);
        EXPECT_TRUE(selection.has_active);
        EXPECT_NE(std::ranges::find(selected_faces, selection.active_face), selected_faces.end());
        EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        if (reversed) {
            EXPECT_EQ(qdr_test::document_fingerprint(document), expected_document);
        } else {
            expected_document = qdr_test::document_fingerprint(document);
        }
    }
}

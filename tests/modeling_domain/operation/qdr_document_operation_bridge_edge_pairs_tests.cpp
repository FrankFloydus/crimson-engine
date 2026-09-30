////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_atomicity_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>

using namespace quader_poly;

namespace {

Document make_coplanar_bridge_document()
{
    Document document;
    document.vertices = {{1, {0.0F, 0.0F, 0.0F}}, {2, {2.0F, 0.0F, 0.0F}},
                         {3, {2.0F, 2.0F, 0.0F}}, {4, {0.0F, 2.0F, 0.0F}},
                         {5, {4.0F, 0.0F, 0.0F}}, {6, {6.0F, 0.0F, 0.0F}},
                         {7, {6.0F, 2.0F, 0.0F}}, {8, {4.0F, 2.0F, 0.0F}}};
    document.faces = {{1, {1, 2, 3, 4}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 7, SurfaceShading::Smooth},
                      {2, {5, 6, 7, 8}, {{10, 0}, {11, 0}, {11, 1}, {10, 1}}, 11, SurfaceShading::Flat}};
    document.hard_normal_edges = {make_edge(2, 3)};
    document.soft_normal_edges = {make_edge(5, 8)};
    document.next_vertex_id = 9;
    document.next_face_id = 3;
    return document;
}

void expect_retained_bridge_sources(const Document& document, const Document& source)
{
    Document retained = document;
    retained.vertices.resize(source.vertices.size());
    retained.faces.resize(source.faces.size());
    retained.next_vertex_id = source.next_vertex_id;
    retained.next_face_id = source.next_face_id;
    EXPECT_EQ(qdr_test::document_fingerprint(retained), qdr_test::document_fingerprint(source));
}

float resolved_uv_area(const Face& face)
{
    float doubled_area = 0.0F;
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
        const auto first = apply_face_texture_transform(face.uvs[corner], face.texture_transform);
        const auto second = apply_face_texture_transform(
            face.uvs[(corner + 1U) % face.vertices.size()], face.texture_transform);
        doubled_area += first.x * second.y - second.x * first.y;
    }
    return doubled_area * 0.5F;
}

void expect_resolved_uv(quader::QVec2 stored, const FaceTextureTransform& transform,
                        quader::QVec2 expected)
{
    const auto resolved = apply_face_texture_transform(stored, transform);
    EXPECT_NEAR(resolved.x, expected.x, 0.0001F);
    EXPECT_NEAR(resolved.y, expected.y, 0.0001F);
}

} // namespace

TEST(QdrDocumentOperationBridgeEdgePairsTests, BridgeEdgePairsCreatesFacesBetweenExplicitEdgePairsOneExplicitPairBridgesOppositeOpenCubeEdges) {
	{
		Document document = make_cube({});
		const Face removed_face = document.faces.front();
		document.faces.erase(document.faces.begin());
		Selection selection;
		const Edge first = make_edge(removed_face.vertices[0], removed_face.vertices[1]);
		const Edge second = make_edge(removed_face.vertices[2], removed_face.vertices[3]);

		const OperationResult result = bridge_edge_pairs(document, selection, { { first, second } }, 2);

		EXPECT_TRUE(result.changed);

		qdr_test::expect_valid_polygon_document_after_success(document, result);
                EXPECT_TRUE(selection.mode == SelectionMode::Face);
                EXPECT_TRUE(selection.faces.size() == 2U);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, first) == 2);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, second) == 2);
		EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	}

	

	
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, RejectsExplicitPairsOfNonBoundaryCubeEdgesAtomically) {
	

	{
		Document document = make_cube({});
		const std::size_t face_count_before = document.faces.size();
		Selection selection;
		const Edge first = make_edge(document.faces.front().vertices[0], document.faces.front().vertices[1]);
		const Edge second = make_edge(document.faces.front().vertices[2], document.faces.front().vertices[3]);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, first) == 2);
		EXPECT_TRUE(qdr_test::edge_incidence_count(document, second) == 2);

        const auto source = qdr_test::document_fingerprint(document);
        const auto source_selection = qdr_test::selection_fingerprint(selection);

		const OperationResult result = bridge_edge_pairs(document, selection, { { first, second } }, 1);

        EXPECT_FALSE(result.success);
        EXPECT_FALSE(result.changed);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(document.faces.size(), face_count_before);
        EXPECT_EQ(qdr_test::document_fingerprint(document), source);
        EXPECT_EQ(qdr_test::selection_fingerprint(selection), source_selection);
	}

	
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, BridgeEdgePairsCreatesFacesBetweenExplicitEdgePairsEmptyPairListFailsWithoutMutating) {
	

	

	{
		Document document = make_cube({});
		Selection selection;

		const OperationResult empty_result = bridge_edge_pairs(document, selection, {}, 1);
		EXPECT_FALSE(empty_result.changed);
		EXPECT_TRUE(empty_result.message == "Bridge needs one or more selected edge pairs.");
	}
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, SteppedRailsProgressBetweenBothDonorCharts)
{
    Document document = make_coplanar_bridge_document();
    const Document source = document;
    Selection selection;

    const auto result = bridge_edge_pairs(document, selection, {{make_edge(2, 3), make_edge(5, 8)}}, 3);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(selection.faces.size(), 3U);
    for (std::size_t strip = 0; strip < selection.faces.size(); ++strip) {
        const Face* face = find_face(document, selection.faces[strip]);
        ASSERT_NE(face, nullptr);
        ASSERT_EQ(face->uvs.size(), 4U);
        const float start_u = 1.0F + 3.0F * static_cast<float>(strip);
        expect_resolved_uv(face->uvs[0], face->texture_transform, {start_u, 1.0F});
        expect_resolved_uv(face->uvs[1], face->texture_transform, {start_u, 0.0F});
        expect_resolved_uv(face->uvs[2], face->texture_transform, {start_u + 3.0F, 0.0F});
        expect_resolved_uv(face->uvs[3], face->texture_transform, {start_u + 3.0F, 1.0F});
        EXPECT_NEAR(std::abs(resolved_uv_area(*face)), 3.0F, 0.0001F);
        EXPECT_EQ(face->material_slot, 7U);
        EXPECT_EQ(face->normal_shading, SurfaceShading::Smooth);
    }
    expect_retained_bridge_sources(document, source);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, ResolvedRailInterpolationRetainsTransformsInEitherPairOrder)
{
    for (const bool reversed : {false, true}) {
        SCOPED_TRACE(reversed);
        Document document = make_coplanar_bridge_document();
        document.faces[0].texture_transform = {{-2.0F, 0.5F}, {0.125F, -0.25F}, 35.0F};
        document.faces[1].texture_transform = {{0.5F, -1.5F}, {-0.25F, 0.75F}, -20.0F};
        const Document source = document;
        const Face& first = source.faces[reversed ? 1U : 0U];
        const Face& second = source.faces[reversed ? 0U : 1U];
        const std::array<std::size_t, 2> first_corners = reversed ? std::array<std::size_t, 2>{0, 3} :
            std::array<std::size_t, 2>{2, 1};
        const std::array<std::size_t, 2> second_corners = reversed ? std::array<std::size_t, 2>{1, 2} :
            std::array<std::size_t, 2>{3, 0};
        Selection selection;
        const Edge left = make_edge(2, 3);
        const Edge right = make_edge(5, 8);

        const auto result = bridge_edge_pairs(document, selection,
            {{reversed ? right : left, reversed ? left : right}}, 3);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_EQ(selection.faces.size(), 3U);
        for (std::size_t strip = 0; strip < selection.faces.size(); ++strip) {
            const Face* face = find_face(document, selection.faces[strip]);
            ASSERT_NE(face, nullptr);
            ASSERT_EQ(face->uvs.size(), 4U);
            EXPECT_EQ(face->texture_transform, first.texture_transform);
            EXPECT_EQ(face->material_slot, first.material_slot);
            EXPECT_EQ(face->normal_shading, first.normal_shading);
            for (std::size_t corner = 0; corner < 4U; ++corner) {
                const std::size_t rail = corner == 0U || corner == 3U ? 0U : 1U;
                const float factor = static_cast<float>(strip + (corner < 2U ? 0U : 1U)) / 3.0F;
                const auto start = apply_face_texture_transform(first.uvs[first_corners[rail]], first.texture_transform);
                const auto end = apply_face_texture_transform(second.uvs[second_corners[rail]], second.texture_transform);
                expect_resolved_uv(face->uvs[corner], face->texture_transform,
                    {start.x + (end.x - start.x) * factor, start.y + (end.y - start.y) * factor});
                if (strip == 0U && corner < 2U) {
                    EXPECT_FLOAT_EQ(face->uvs[corner].x, first.uvs[first_corners[rail]].x);
                    EXPECT_FLOAT_EQ(face->uvs[corner].y, first.uvs[first_corners[rail]].y);
                }
            }
        }
        expect_retained_bridge_sources(document, source);
    }
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, MissingDonorUvsUseGeneratedChartsWithoutAuthoringSourceFaces)
{
    for (const int missing_charts : {0, 1, 2}) {
        SCOPED_TRACE(missing_charts);
        Document document = make_coplanar_bridge_document();
        // Align authored and generated chart axes so rail interpolation has a
        // nonzero span instead of cancelling opposing endpoint chart directions.
        for (std::size_t face_index = 0; face_index < document.faces.size(); ++face_index) {
            Face& face = document.faces[face_index];
            const float offset = face_index == 0U ? -2.0F : 10.0F;
            for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
                const auto generated = generated_texture_uv(
                    find_vertex(document, face.vertices[corner])->position, {0, 0, 1});
                face.uvs[corner] = {generated.x + offset, generated.y};
            }
        }
        if (missing_charts != 2) {
            document.faces[0].uvs.clear();
        }
        if (missing_charts != 1) {
            document.faces[1].uvs.clear();
        }
        const Document source = document;
        Selection selection;

        const auto result = bridge_edge_pairs(document, selection, {{make_edge(2, 3), make_edge(5, 8)}}, 3);

        ASSERT_TRUE(result.success) << result.message;
        ASSERT_EQ(selection.faces.size(), 3U);
        for (std::size_t strip = 0; strip < selection.faces.size(); ++strip) {
            const Face* face = find_face(document, selection.faces[strip]);
            ASSERT_NE(face, nullptr);
            ASSERT_EQ(face->uvs.size(), 4U);
            EXPECT_GT(std::abs(resolved_uv_area(*face)), 0.000001F);
            for (std::size_t corner = 0; corner < 4U; ++corner) {
                const bool upper = corner == 0U || corner == 3U;
                const std::size_t first_corner = upper ? 2U : 1U;
                const std::size_t second_corner = upper ? 3U : 0U;
                const auto start = source.faces[0].uvs.empty() ? generated_texture_uv(
                    find_vertex(source, source.faces[0].vertices[first_corner])->position, {0, 0, 1}) :
                    source.faces[0].uvs[first_corner];
                const auto end = source.faces[1].uvs.empty() ? generated_texture_uv(
                    find_vertex(source, source.faces[1].vertices[second_corner])->position, {0, 0, 1}) :
                    source.faces[1].uvs[second_corner];
                const float factor = static_cast<float>(strip + (corner < 2U ? 0U : 1U)) / 3.0F;
                expect_resolved_uv(face->uvs[corner], face->texture_transform,
                    {start.x + (end.x - start.x) * factor, start.y + (end.y - start.y) * factor});
            }
        }
        expect_retained_bridge_sources(document, source);
    }
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, SteppedTriangleFanRetainsTheSharedCornerAndMovingRailUvs)
{
    Document document;
    document.vertices = {{1, {0, 0, 0}}, {2, {0, 2, 0}}, {3, {-1, 1, 0}},
                         {4, {2, 0, 0}}, {5, {1, -1, 0}}};
    document.faces = {{1, {1, 2, 3}, {{0, 0}, {0, 2}, {-1, 1}}},
                      {2, {4, 1, 5}, {{2, 0}, {100, 100}, {1, -1}}}};
    document.next_vertex_id = 6;
    document.next_face_id = 3;
    const Document source = document;
    Selection selection;

    const auto result = bridge_edge_pairs(document, selection, {{make_edge(1, 2), make_edge(1, 4)}}, 3);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(selection.faces.size(), 3U);
    EXPECT_EQ(document.vertices.size(), 7U);
    for (std::size_t strip = 0; strip < selection.faces.size(); ++strip) {
        const Face* face = find_face(document, selection.faces[strip]);
        ASSERT_NE(face, nullptr);
        ASSERT_EQ(face->uvs.size(), 3U);
        EXPECT_EQ(face->vertices[1], 1U);
        expect_resolved_uv(face->uvs[1], face->texture_transform, {0, 0});
        const float previous = static_cast<float>(strip) / 3.0F;
        const float current = static_cast<float>(strip + 1U) / 3.0F;
        expect_resolved_uv(face->uvs[0], face->texture_transform, {2.0F * previous, 2.0F * (1.0F - previous)});
        expect_resolved_uv(face->uvs[2], face->texture_transform, {2.0F * current, 2.0F * (1.0F - current)});
        EXPECT_GT(std::abs(resolved_uv_area(*face)), 0.000001F);
    }
    expect_retained_bridge_sources(document, source);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

TEST(QdrDocumentOperationBridgeEdgePairsTests, SharedGeometryRailsRetainEachPairsUvSeamAndDonorAttributes)
{
    Document document;
    document.vertices = {{1, {0, 1, 0}}, {2, {1, 0, 0}}, {3, {1, 1, 0}}, {4, {1, 2, 0}},
                         {5, {3, 0, 0}}, {6, {3, 1, 0}}, {7, {3, 2, 0}}, {8, {4, 1, 0}}};
    document.faces = {{1, {1, 2, 3}, {{-1, 1}, {0, 0}, {0, 1}}, 7, SurfaceShading::Smooth},
                      {2, {1, 3, 4}, {{99, 10}, {100, 10}, {100, 11}}, 11, SurfaceShading::Flat},
                      {3, {5, 8, 6}, {{10, 0}, {11, 1}, {10, 1}}},
                      {4, {6, 8, 7}, {{200, 10}, {201, 10}, {200, 11}}}};
    document.next_vertex_id = 9;
    document.next_face_id = 5;
    document.hard_normal_edges = {make_edge(2, 3), make_edge(6, 7)};
    document.soft_normal_edges = {make_edge(3, 4), make_edge(5, 6)};
    const Document source = document;
    Selection selection;

    const auto result = bridge_edge_pairs(document, selection,
        {{make_edge(2, 3), make_edge(5, 6)}, {make_edge(3, 4), make_edge(6, 7)}}, 3);

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(selection.faces.size(), 6U);
    EXPECT_EQ(document.vertices.size(), 14U);
    for (std::size_t strip = 0; strip < 3U; ++strip) {
        const Face* lower = find_face(document, selection.faces[strip]);
        const Face* upper = find_face(document, selection.faces[strip + 3U]);
        ASSERT_NE(lower, nullptr);
        ASSERT_NE(upper, nullptr);
        ASSERT_EQ(lower->uvs.size(), 4U);
        ASSERT_EQ(upper->uvs.size(), 4U);
        EXPECT_EQ(lower->vertices[0], upper->vertices[1]);
        EXPECT_EQ(lower->vertices[3], upper->vertices[2]);
        const float previous = static_cast<float>(strip) / 3.0F;
        const float current = static_cast<float>(strip + 1U) / 3.0F;
        expect_resolved_uv(lower->uvs[0], lower->texture_transform, {10.0F * previous, 1});
        expect_resolved_uv(lower->uvs[3], lower->texture_transform, {10.0F * current, 1});
        expect_resolved_uv(upper->uvs[1], upper->texture_transform, {100.0F + 100.0F * previous, 10});
        expect_resolved_uv(upper->uvs[2], upper->texture_transform, {100.0F + 100.0F * current, 10});
        EXPECT_EQ(lower->material_slot, 7U);
        EXPECT_EQ(lower->normal_shading, SurfaceShading::Smooth);
        EXPECT_EQ(upper->material_slot, 11U);
        EXPECT_EQ(upper->normal_shading, SurfaceShading::Flat);
    }
    expect_retained_bridge_sources(document, source);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
}

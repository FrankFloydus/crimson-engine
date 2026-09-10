////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>

using namespace quader_poly;

TEST(QdrDocumentOperationBevelVerticesTests, RetainedFaceInterpolatesAuthoredCornersWithoutResettingTheChart) {
    qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
    Face& source = fixture.document.faces.front();
    source.uvs = {{2.0F, 3.0F}, {6.0F, 4.0F}, {9.0F, 10.0F}, {1.0F, 8.0F}};
    const Face original = source;
    Selection selection = qdr_test::vertex_selection({fixture.bottom_left});

    const OperationResult result = bevel_selected_vertices(fixture.document, selection, 0.5F);

    ASSERT_TRUE(result.changed) << result.message;
    const Face* retained = find_face(fixture.document, fixture.face);
    ASSERT_NE(retained, nullptr);
    ASSERT_EQ(retained->uvs.size(), retained->vertices.size());
    for (std::size_t corner = 0; corner < retained->vertices.size(); ++corner) {
        const ElementId id = retained->vertices[corner];
        const auto old_corner = std::ranges::find(original.vertices, id);
        if (old_corner != original.vertices.end()) {
            const auto index = static_cast<std::size_t>(old_corner - original.vertices.begin());
            EXPECT_FLOAT_EQ(retained->uvs[corner].x, original.uvs[index].x);
            EXPECT_FLOAT_EQ(retained->uvs[corner].y, original.uvs[index].y);
        } else {
            const Vertex* vertex = find_vertex(fixture.document, id);
            ASSERT_NE(vertex, nullptr);
            const quader::QVec2 expected = vertex->position.y == -1.0F
                ? quader::QVec2{3.0F, 3.25F}
                : quader::QVec2{1.75F, 4.25F};
            EXPECT_NEAR(retained->uvs[corner].x, expected.x, 0.0001F);
            EXPECT_NEAR(retained->uvs[corner].y, expected.y, 0.0001F);
        }
    }
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationBevelVerticesTests, BevelVerticesTrimsTheSourceCornerAndSelectsGeneratedReplacements) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left }, fixture.bottom_left);

	const OperationResult result = bevel_selected_vertices(fixture.document, selection, 0.25F);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(find_vertex(fixture.document, fixture.bottom_left) == nullptr);
	EXPECT_TRUE(fixture.document.vertices.size() == 5U);
	EXPECT_TRUE(fixture.document.faces.size() == 1U);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 2U);
	for (ElementId vertex_id : selection.vertices) {
		EXPECT_TRUE(find_vertex(fixture.document, vertex_id) != nullptr);
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationBevelVerticesTests, BevelVerticesOnACubeCreatesAnOutwardCapAndKeepsTheHullClosed) {
	Document document = make_cube({});
	const FaceTextureTransform texture{{2.0F, -0.5F}, {0.25F, 0.75F}, 55.0F};
	for (Face& face : document.faces) {
		face.texture_transform = texture;
	}
	const ElementId vertex_id = document.vertices.front().id;
	const quader::QVec3 vertex_position = document.vertices.front().position;
	const std::size_t vertex_count_before = document.vertices.size();
	const std::size_t face_count_before = document.faces.size();
	Selection selection = qdr_test::vertex_selection({ vertex_id }, vertex_id);

	const OperationResult result = bevel_selected_vertices(document, selection, 16.0F);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.vertices.size() == vertex_count_before + 2U);
	EXPECT_TRUE(document.faces.size() == face_count_before + 1U);
	for (const Face& face : document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
	}
	EXPECT_TRUE(find_vertex(document, vertex_id) == nullptr);
        EXPECT_TRUE(selection.mode == SelectionMode::Vertex);
        EXPECT_TRUE(selection.vertices.size() == 3U);
	for (ElementId split_vertex_id : selection.vertices) {
		const Vertex* split_vertex = find_vertex(document, split_vertex_id);
		EXPECT_TRUE(split_vertex != nullptr);
		const float split_distance = length(split_vertex->position - vertex_position);
		EXPECT_TRUE(split_distance > 0.0F);
		EXPECT_TRUE(split_distance <= 16.0F);
	}

	const auto cap_face = std::find_if(document.faces.begin(), document.faces.end(), [&selection](const Face& face) {
		return face.vertices.size() == selection.vertices.size() && qdr_test::face_contains_all_vertices(face, selection.vertices);
	});
	EXPECT_TRUE(cap_face != document.faces.end());
	EXPECT_TRUE(dot(face_normal(document, *cap_face), vertex_position - qdr_test::face_centroid(document, *cap_face)) > 0.0F);
	EXPECT_TRUE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_edge_is_closed(document));
}

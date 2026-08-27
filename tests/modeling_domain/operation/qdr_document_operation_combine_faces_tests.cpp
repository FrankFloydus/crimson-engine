////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <map>

using namespace quader_poly;

TEST(QdrDocumentOperationCombineFacesTests, CombineFacesRemovesASupportEdgeBetweenAdjacentSelectedFaces) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection split = qdr_test::edge_selection({
		make_edge(fixture.bottom_left, fixture.bottom_right),
		make_edge(fixture.top_right, fixture.top_left),
	});
	EXPECT_TRUE(connect_selected_edges(fixture.document, split).changed);
	qdr_test::expect_valid_polygon_document(fixture.document, "successful topology operation");
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	Selection selection;
        selection.mode = SelectionMode::Face;
        for (const Face& face : fixture.document.faces) {
		selection.faces.push_back(face.id);
	}

	const OperationResult result = combine_selected_faces(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.faces.size() == 1U);
	EXPECT_TRUE(fixture.document.faces.front().vertices.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationCombineFacesTests, CombineFacesRestoresAnInsetFaceIslandWithoutDependingOnDissolveOrder) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection inset_selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	EXPECT_TRUE(inset_selected_faces(fixture.document, inset_selection, 0.2F).changed);
	qdr_test::expect_valid_polygon_document(fixture.document, "successful topology operation");
	EXPECT_TRUE(fixture.document.faces.size() == 5U);

	Selection selection = qdr_test::all_face_selection(fixture.document);
	const OperationResult result = combine_selected_faces(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.faces.size() == 1U);
	EXPECT_TRUE(fixture.document.faces.front().vertices.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST(QdrDocumentOperationCombineFacesTests, CombineFacesKeepsEditedBoundaryUvsInTheMaterialDonorsBaseSpace) {
    qdr_test::QuadFixture fixture = qdr_test::make_quad_document(7);
    Selection split = qdr_test::edge_selection({
        make_edge(fixture.bottom_left, fixture.bottom_right),
        make_edge(fixture.top_right, fixture.top_left),
    });
    ASSERT_TRUE(connect_selected_edges(fixture.document, split).changed);
    ASSERT_EQ(fixture.document.faces.size(), 2U);
    const FaceTextureTransform donor_texture{{2.0F, 0.5F}, {0.25F, -0.75F}, 35.0F};
    fixture.document.faces[0].texture_transform = donor_texture;
    fixture.document.faces[0].uvs = {{0.0F, 0.0F}, {2.0F, 0.0F}, {2.0F, 1.0F}, {0.0F, 1.0F}};
    fixture.document.faces[1].texture_transform = {{-0.5F, 3.0F}, {-1.0F, 0.5F}, -20.0F};
    std::map<ElementId, quader::QVec2> expected_uvs;
    for (const Face& face : fixture.document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId vertex_id = face.vertices[index];
            const Vertex* vertex = find_vertex(fixture.document, vertex_id);
            ASSERT_NE(vertex, nullptr);
            const quader::QVec2 base_uv = face.uvs.empty()
                ? generated_texture_uv(vertex->position, face_normal(fixture.document, face))
                : face.uvs[index];
            expected_uvs.emplace(vertex_id, apply_face_texture_transform(base_uv, face.texture_transform));
        }
    }
    Selection selection = qdr_test::all_face_selection(fixture.document);

    const OperationResult result = combine_selected_faces(fixture.document, selection);

    ASSERT_TRUE(result.changed) << result.message;
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
    ASSERT_EQ(fixture.document.faces.size(), 1U);
    const Face& merged = fixture.document.faces.front();
    EXPECT_EQ(merged.texture_transform, donor_texture);
    ASSERT_EQ(merged.vertices.size(), 4U);
    ASSERT_EQ(merged.uvs.size(), merged.vertices.size());
    for (std::size_t index = 0; index < merged.vertices.size(); ++index) {
        const quader::QVec2 uv = apply_face_texture_transform(merged.uvs[index], merged.texture_transform);
        const quader::QVec2 expected = expected_uvs.at(merged.vertices[index]);
        EXPECT_NEAR(uv.x, expected.x, 0.0001F);
        EXPECT_NEAR(uv.y, expected.y, 0.0001F);
    }
}

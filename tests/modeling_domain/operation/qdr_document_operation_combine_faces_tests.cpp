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
    fixture.document.faces[1].texture_transform = {{-0.5F, 3.0F}, {-1.0F, 0.5F}, -20.0F};
    std::map<ElementId, quader::QVec2> expected_uvs;
    for (Face& face : fixture.document.faces) {
        face.uvs.clear();
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId vertex_id = face.vertices[index];
            const Vertex* vertex = find_vertex(fixture.document, vertex_id);
            ASSERT_NE(vertex, nullptr);
            const quader::QVec2 uv{2.0F * vertex->position.x + 3.0F, 0.5F * vertex->position.y - 4.0F};
            face.uvs.push_back(unapply_face_texture_transform(uv, face.texture_transform));
            expected_uvs.emplace(vertex_id, uv);
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

TEST(QdrDocumentOperationCombineFacesTests, CombineRetainsCustomChartsWithIdentityNumericTransforms) {
    qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
    Selection split = qdr_test::edge_selection({
        make_edge(fixture.bottom_left, fixture.bottom_right),
        make_edge(fixture.top_right, fixture.top_left)});
    ASSERT_TRUE(connect_selected_edges(fixture.document, split).changed);
    for (Face& face : fixture.document.faces) {
        face.uvs.clear();
        for (const ElementId id : face.vertices) {
            const auto position = find_vertex(fixture.document, id)->position;
            face.uvs.push_back({3.0F * position.x + 7.0F, 2.0F * position.y - 5.0F});
        }
    }
    Selection selection = qdr_test::all_face_selection(fixture.document);

    const OperationResult result = combine_selected_faces(fixture.document, selection);

    ASSERT_TRUE(result.changed) << result.message;
    ASSERT_EQ(fixture.document.faces.size(), 1U);
    const Face& face = fixture.document.faces.front();
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
        const auto position = find_vertex(fixture.document, face.vertices[corner])->position;
        EXPECT_NEAR(face.uvs[corner].x, 3.0F * position.x + 7.0F, 0.0001F);
        EXPECT_NEAR(face.uvs[corner].y, 2.0F * position.y - 5.0F, 0.0001F);
    }
}

TEST(QdrDocumentOperationCombineFacesTests, CombineRejectsAnAuthoredInteriorSeamWithoutChangingFaces) {
    qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
    Selection split = qdr_test::edge_selection({
        make_edge(fixture.bottom_left, fixture.bottom_right),
        make_edge(fixture.top_right, fixture.top_left)});
    ASSERT_TRUE(connect_selected_edges(fixture.document, split).changed);
    for (std::size_t index = 0; index < fixture.document.faces.size(); ++index) {
        Face& face = fixture.document.faces[index];
        face.uvs.clear();
        for (const ElementId id : face.vertices) {
            const auto position = find_vertex(fixture.document, id)->position;
            face.uvs.push_back({position.x + 10.0F * static_cast<float>(index), position.y});
        }
    }
    const Document original = fixture.document;
    Selection selection = qdr_test::all_face_selection(fixture.document);
    const auto original_selection = selection.faces;

    const OperationResult result = combine_selected_faces(fixture.document, selection);

    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.success);
    ASSERT_EQ(fixture.document.faces.size(), original.faces.size());
    for (std::size_t index = 0; index < original.faces.size(); ++index) {
        EXPECT_EQ(fixture.document.faces[index].vertices, original.faces[index].vertices);
        ASSERT_EQ(fixture.document.faces[index].uvs.size(), original.faces[index].uvs.size());
        for (std::size_t corner = 0; corner < original.faces[index].uvs.size(); ++corner) {
            EXPECT_FLOAT_EQ(fixture.document.faces[index].uvs[corner].x, original.faces[index].uvs[corner].x);
            EXPECT_FLOAT_EQ(fixture.document.faces[index].uvs[corner].y, original.faces[index].uvs[corner].y);
        }
    }
    EXPECT_EQ(selection.faces, original_selection);
    EXPECT_EQ(fixture.document.next_face_id, original.next_face_id);
    EXPECT_EQ(fixture.document.next_vertex_id, original.next_vertex_id);
}

TEST(QdrDocumentOperationCombineFacesTests, CombineRetainsACollinearBoundaryUvBreakpoint) {
    Document document;
    const ElementId a = add_vertex(document, {0.0F, 0.0F, 0.0F});
    const ElementId d = add_vertex(document, {1.0F, 0.0F, 0.0F});
    const ElementId b = add_vertex(document, {2.0F, 0.0F, 0.0F});
    const ElementId c = add_vertex(document, {0.0F, 2.0F, 0.0F});
    const ElementId first = add_face(document, std::array{a, d, c});
    const ElementId second = add_face(document, std::array{d, b, c});
    find_face(document, first)->uvs = {{0.0F, 0.0F}, {0.1F, 0.0F}, {0.0F, 1.0F}};
    find_face(document, second)->uvs = {{0.1F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
    Selection selection = qdr_test::face_selection({first, second});

    const OperationResult result = combine_selected_faces(document, selection);

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

TEST(QdrDocumentOperationCombineFacesTests, CombineValidatesContinuousBilinearSubpatchesAfterBoundarySimplification) {
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
    Selection selection = qdr_test::face_selection({first, second});

    const OperationResult result = combine_selected_faces(document, selection);

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

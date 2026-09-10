////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationExtractFacesTests, ExtractFacesRemovesSourceFacesAndReturnsASelectedExtractedDocument) {
	Document document = make_cube({});
	const FaceTextureTransform texture{{2.0F, -0.5F}, {0.25F, 0.75F}, 70.0F};
	document.faces.front().texture_transform = texture;
	ensure_face_uvs(document);
	const std::vector<quader::QVec2> base_uvs = document.faces.front().uvs;
	Selection selection = qdr_test::face_selection({ document.faces.front().id }, document.faces.front().id);

	const ExtractFacesResult result = extract_selected_faces(document, selection);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.faces.size() == 5U);
	ASSERT_EQ(result.extracted_document.faces.size(), 1U);
	EXPECT_EQ(result.extracted_document.faces.front().texture_transform, texture);
	const auto &extracted_uvs = result.extracted_document.faces.front().uvs;
	ASSERT_EQ(extracted_uvs.size(), base_uvs.size());
	for (std::size_t index = 0U; index < base_uvs.size(); ++index) {
		EXPECT_EQ(extracted_uvs[index].x, base_uvs[index].x);
		EXPECT_EQ(extracted_uvs[index].y, base_uvs[index].y);
	}
        EXPECT_TRUE(result.extracted_selection.mode == SelectionMode::Face);
        EXPECT_TRUE(result.extracted_selection.faces.size() == 1U);
	EXPECT_TRUE(result.extracted_selection.active_face == result.extracted_selection.faces.front());
	EXPECT_TRUE(find_face(result.extracted_document, result.extracted_selection.active_face) != nullptr);
	EXPECT_TRUE(face_perimeter_info(result.extracted_document, result.extracted_selection.active_face).has_only_open_edges());
	EXPECT_TRUE(qdr_test::open_edge_count(document) == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(result.extracted_document));
}

TEST(QdrDocumentOperationExtractFacesTests, ExtractCopiesFaceShadingAndExactEdgeNormalModes) {
	Document document = make_cube({});
	Face& source_face = document.faces.front();
	source_face.normal_shading = SurfaceShading::Smooth;
	const Edge hard_source = make_edge(source_face.vertices[0], source_face.vertices[1]);
	const Edge soft_source = make_edge(source_face.vertices[2], source_face.vertices[3]);
	const ElementId source_face_id = source_face.id;
	const std::size_t source_vertex_count = source_face.vertices.size();
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, hard_source, EdgeNormalMode::Hard));
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, soft_source, EdgeNormalMode::Soft));
	Selection selection = qdr_test::face_selection({ source_face_id }, source_face_id);

	const ExtractFacesResult result = extract_selected_faces(document, selection);

	ASSERT_TRUE(result.changed) << result.message;
	ASSERT_EQ(result.extracted_document.faces.size(), 1U);
	const Face& extracted = result.extracted_document.faces.front();
	EXPECT_EQ(extracted.normal_shading, SurfaceShading::Smooth);
	ASSERT_EQ(extracted.vertices.size(), source_vertex_count);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(result.extracted_document, make_edge(extracted.vertices[0], extracted.vertices[1])),
		EdgeNormalMode::Hard);
	EXPECT_EQ(
		qdr_test::edge_normal_mode(result.extracted_document, make_edge(extracted.vertices[2], extracted.vertices[3])),
		EdgeNormalMode::Soft);
	qdr_test::expect_valid_polygon_document(
		result.extracted_document, "extract should preserve edge-normal state");
}

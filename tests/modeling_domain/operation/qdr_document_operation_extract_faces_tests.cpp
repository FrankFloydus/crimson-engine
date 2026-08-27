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

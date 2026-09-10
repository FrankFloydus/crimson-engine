////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationInsetElementsTests, FaceInsetRetainsTheCopiedCapsAuthoredUvChart) {
    qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
    const std::vector<quader::QVec2> authored_uvs{{2.0F, 3.0F}, {6.0F, 4.0F}, {9.0F, 10.0F}, {1.0F, 8.0F}};
    fixture.document.faces.front().uvs = authored_uvs;
    Selection selection = qdr_test::face_selection({fixture.face});
    Transform3 transform;
    transform.x_axis = {0.5F, 0.0F, 0.0F};
    transform.y_axis = {0.0F, 0.5F, 0.0F};

    const OperationResult result = inset_selected_elements(fixture.document, selection, transform, {});

    ASSERT_TRUE(result.changed) << result.message;
    const Face* cap = find_face(fixture.document, fixture.face);
    ASSERT_NE(cap, nullptr);
    ASSERT_EQ(cap->uvs.size(), authored_uvs.size());
    for (std::size_t corner = 0; corner < authored_uvs.size(); ++corner) {
        EXPECT_FLOAT_EQ(cap->uvs[corner].x, authored_uvs[corner].x);
        EXPECT_FLOAT_EQ(cap->uvs[corner].y, authored_uvs[corner].y);
    }
    qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

TEST(QdrDocumentOperationInsetElementsTests, InsetSelectedEdgeElementsTransformsACopiedEdgeIntoANewFaceStrip) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const FaceTextureTransform texture{{2.0F, -0.5F}, {0.25F, 0.75F}, -25.0F};
	fixture.document.faces.front().texture_transform = texture;
	const Edge source = make_edge(fixture.bottom_left, fixture.bottom_right);
	Selection selection = qdr_test::edge_selection({ source }, source);
	Transform3 transform;
	transform.origin = { 0.0F, -0.5F, 0.0F };

	const OperationResult result = inset_selected_elements(fixture.document, selection, transform, {});

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
        EXPECT_TRUE(selection.mode == SelectionMode::Edge);
        EXPECT_TRUE(selection.edges.size() == 1U);
	EXPECT_TRUE(fixture.document.faces.size() == 2U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
	}
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

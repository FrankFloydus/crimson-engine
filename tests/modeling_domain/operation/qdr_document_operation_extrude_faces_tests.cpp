////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationExtrudeFacesTests, ExtrudeFacesCreatesACapAndSideWallsWhileKeepingTheCapSelected) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(11);
	const FaceTextureTransform texture{{-2.0F, 0.5F}, {0.25F, 0.75F}, 25.0F};
	fixture.document.faces.front().texture_transform = texture;
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = extrude_selected_faces(fixture.document, selection, 0.5F);

	EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(result.created.vertices.size() == 4U);
	EXPECT_TRUE(result.created.faces.size() == 4U);
	EXPECT_TRUE(result.created.edges.empty());
	EXPECT_TRUE(result.deleted.empty());
	EXPECT_TRUE(result.affected.vertices.empty());
	EXPECT_TRUE(result.affected.edges.empty());
	EXPECT_TRUE(result.affected.faces.size() == 1U);
	EXPECT_TRUE(result.affected.faces.front() == fixture.face);
	EXPECT_TRUE(fixture.document.vertices.size() == 8U);
	EXPECT_TRUE(fixture.document.faces.size() == 5U);
	for (const Face& face : fixture.document.faces) {
		EXPECT_EQ(face.texture_transform, texture);
	}
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 1U);
	EXPECT_TRUE(selection.faces.front() == fixture.face);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

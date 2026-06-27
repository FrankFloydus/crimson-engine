////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationExtrudeFacesTests, ExtrudeFacesCreatesACapAndSideWallsWhileKeepingTheCapSelected) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document(11);
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = extrude_selected_faces(fixture.document, selection, 0.5F);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(fixture.document.vertices.size() == 8U);
	EXPECT_TRUE(fixture.document.faces.size() == 5U);
	EXPECT_TRUE(selection.mode == SelectionMode::face);
	EXPECT_TRUE(selection.faces.size() == 1U);
	EXPECT_TRUE(selection.faces.front() == fixture.face);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

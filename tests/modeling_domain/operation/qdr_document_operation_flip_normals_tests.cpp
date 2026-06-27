////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationFlipNormalsTests, FlipNormalsReversesOnlySelectedFaceWinding) {
	Document document = make_cube({});
	Selection selection = qdr_test::all_face_selection(document);
	EXPECT_TRUE(qdr_test::all_faces_point_relative_to_center(document, true));

	const OperationResult result = flip_selected_face_normals(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(qdr_test::all_faces_point_relative_to_center(document, false));
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

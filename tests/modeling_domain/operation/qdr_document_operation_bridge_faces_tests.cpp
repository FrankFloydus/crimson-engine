////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationBridgeFacesTests, BridgeFacesConnectsTwoOpenFaceIslands) {
	Document document = qdr_test::make_parallel_quad_document();
	Selection selection = qdr_test::all_face_selection(document);

	const OperationResult result = bridge_selected_faces(document, selection);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.faces.size() == 6U);
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationBridgeFacesTests, InterpolatedBridgeFacesCreatesOneRingPerStep) {
	Document document = qdr_test::make_parallel_quad_document();
	Selection selection = qdr_test::all_face_selection(document);

	const OperationResult result = bridge_selected_faces(document, selection, 3);

	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(document.faces.size() == 14U);
	EXPECT_TRUE(selection.faces.size() == 12U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

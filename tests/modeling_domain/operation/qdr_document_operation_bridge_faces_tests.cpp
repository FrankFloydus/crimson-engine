////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationBridgeFacesTests, BridgeFacesConnectsTwoOpenFaceIslands) {
	Document document = qdr_test::make_parallel_quad_document();
	const FaceTextureTransform texture{{2.0F, -0.5F}, {0.25F, 0.75F}, 55.0F};
	document.faces.front().texture_transform = texture;
	Selection selection = qdr_test::all_face_selection(document);

	const OperationResult result = bridge_selected_faces(document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.faces.size() == 6U);
	for (const ElementId id : selection.faces) {
		const Face* face = find_face(document, id);
		ASSERT_NE(face, nullptr);
		EXPECT_EQ(face->texture_transform, texture);
	}
        EXPECT_TRUE(selection.mode == SelectionMode::Face);
        EXPECT_TRUE(selection.faces.size() == 4U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationBridgeFacesTests, InterpolatedBridgeFacesCreatesOneRingPerStep) {
	Document document = qdr_test::make_parallel_quad_document();
	Selection selection = qdr_test::all_face_selection(document);

	const OperationResult result = bridge_selected_faces(document, selection, 3);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(document, result);
	EXPECT_TRUE(document.faces.size() == 14U);
	EXPECT_TRUE(selection.faces.size() == 12U);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST(QdrDocumentOperationBridgeFacesTests, BridgeRetainsBoundaryModesAndLeavesNewRailsDefault) {
	Document document = qdr_test::make_parallel_quad_document();
	const Edge hard_boundary =
		qdr_test::face_edges_for_test(document.faces[0]).front();
	const Edge soft_boundary =
		qdr_test::face_edges_for_test(document.faces[1]).front();
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, hard_boundary, EdgeNormalMode::Hard));
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(document, soft_boundary, EdgeNormalMode::Soft));
	Selection selection = qdr_test::all_face_selection(document);

	const OperationResult result = bridge_selected_faces(document, selection, 2);

	ASSERT_TRUE(result.changed) << result.message;
	EXPECT_EQ(qdr_test::edge_normal_mode(document, hard_boundary), EdgeNormalMode::Hard);
	EXPECT_EQ(qdr_test::edge_normal_mode(document, soft_boundary), EdgeNormalMode::Soft);
	EXPECT_EQ(document.hard_normal_edges, std::vector<Edge> { hard_boundary });
	EXPECT_EQ(document.soft_normal_edges, std::vector<Edge> { soft_boundary });
	qdr_test::expect_valid_polygon_document_after_success(document, result);
}

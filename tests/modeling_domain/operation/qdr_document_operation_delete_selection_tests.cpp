////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationDeleteSelectionTests, DeleteSelectionPrunesAffectedFacesAndUnusedVertices) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const ElementId loose_vertex = add_vertex(fixture.document, { 4.0F, 0.0F, 0.0F });
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left });

	const OperationResult result = delete_selection(fixture.document, selection);

	EXPECT_TRUE(result.changed);

	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(result.message.empty());
	EXPECT_TRUE(result.created.empty());
	EXPECT_TRUE(result.affected.empty());
	EXPECT_TRUE(result.deleted.vertices.size() == 5U);
	EXPECT_TRUE(result.deleted.edges.size() == 4U);
	EXPECT_TRUE(result.deleted.faces.size() == 1U);
	EXPECT_TRUE(qdr_test::contains_element_id(result.deleted.vertices, fixture.bottom_left));
	EXPECT_TRUE(qdr_test::contains_element_id(result.deleted.vertices, loose_vertex));
	EXPECT_TRUE(qdr_test::contains_element_id(result.deleted.faces, fixture.face));
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
	EXPECT_TRUE(fixture.document.faces.empty());
	EXPECT_TRUE(find_vertex(fixture.document, loose_vertex) == nullptr);
	EXPECT_TRUE(fixture.document.vertices.empty());
}

TEST(QdrDocumentOperationDeleteSelectionTests, DeletePrunesHardAndSoftStateForRemovedEdges) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const Face& face = fixture.document.faces.front();
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(
		fixture.document, make_edge(face.vertices[0], face.vertices[1]), EdgeNormalMode::Hard));
	ASSERT_TRUE(qdr_test::set_edge_normal_mode(
		fixture.document, make_edge(face.vertices[2], face.vertices[3]), EdgeNormalMode::Soft));
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = delete_selection(fixture.document, selection);

	ASSERT_TRUE(result.changed) << result.message;
	EXPECT_TRUE(fixture.document.hard_normal_edges.empty());
	EXPECT_TRUE(fixture.document.soft_normal_edges.empty());
	qdr_test::expect_valid_polygon_document_after_success(fixture.document, result);
}

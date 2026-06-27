////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

TEST(QdrDocumentOperationSnapVerticesToActiveTests, SnapVerticesToActiveMovesSelectedVerticesWithoutChangingTopology) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection(
		{ fixture.bottom_left, fixture.bottom_right },
		fixture.bottom_left);

	const OperationResult result = snap_selected_vertices_to_active(fixture.document, selection);

	EXPECT_TRUE(result.changed);
	const Vertex* active = find_vertex(fixture.document, fixture.bottom_left);
	const Vertex* bottom_right = find_vertex(fixture.document, fixture.bottom_right);
	const Vertex* top_right = find_vertex(fixture.document, fixture.top_right);
	EXPECT_TRUE(active != nullptr);
	EXPECT_TRUE(bottom_right != nullptr);
	EXPECT_TRUE(top_right != nullptr);
	EXPECT_TRUE(qdr_test::vec_nearly_equal(bottom_right->position, active->position));
	EXPECT_TRUE(fixture.document.vertices.size() == 4U);
	EXPECT_TRUE(fixture.document.faces.size() == 1U);
	EXPECT_TRUE(selection.has_active);
	EXPECT_TRUE(selection.vertices.size() == 2U);
	EXPECT_TRUE(selection.active_vertex == fixture.bottom_left);
}

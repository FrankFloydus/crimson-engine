////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("snap vertices to active moves selected vertices without changing topology", "[operation][snap_vertices_to_active]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection(
		{ fixture.bottom_left, fixture.bottom_right },
		fixture.bottom_left);

	const OperationResult result = snap_selected_vertices_to_active(fixture.document, selection);

	REQUIRE(result.changed);
	const Vertex* active = find_vertex(fixture.document, fixture.bottom_left);
	const Vertex* bottom_right = find_vertex(fixture.document, fixture.bottom_right);
	const Vertex* top_right = find_vertex(fixture.document, fixture.top_right);
	REQUIRE(active != nullptr);
	REQUIRE(bottom_right != nullptr);
	REQUIRE(top_right != nullptr);
	REQUIRE(qdr_test::vec_nearly_equal(bottom_right->position, active->position));
	REQUIRE(fixture.document.vertices.size() == 4U);
	REQUIRE(fixture.document.faces.size() == 1U);
	REQUIRE(selection.has_active);
	REQUIRE(selection.vertices.size() == 2U);
	REQUIRE(selection.active_vertex == fixture.bottom_left);
}

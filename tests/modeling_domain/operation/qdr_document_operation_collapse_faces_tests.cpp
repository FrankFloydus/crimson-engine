////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("collapse faces turns selected face area into one selected vertex", "[operation][collapse_faces]")
{
	Document document = make_cube({});
	Selection selection = qdr_test::face_selection({ document.faces.front().id }, document.faces.front().id);
	const std::size_t vertex_count_before = document.vertices.size();

	const OperationResult result = collapse_selected_faces(document, selection);

	REQUIRE(result.changed);
	REQUIRE(document.vertices.size() < vertex_count_before);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 1U);
	REQUIRE(find_vertex(document, selection.vertices.front()) != nullptr);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}

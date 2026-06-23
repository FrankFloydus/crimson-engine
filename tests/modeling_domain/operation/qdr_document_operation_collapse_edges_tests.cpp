////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("collapse edges replaces an edge with one centered survivor vertex", "[operation][collapse_edges]")
{
	Document document = make_cube({});
	const Edge source_edge = document_edges(document).front();
	const Vertex* first = find_vertex(document, source_edge.a);
	const Vertex* second = find_vertex(document, source_edge.b);
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	const quader::QVec3 midpoint = (first->position + second->position) / 2.0F;
	Selection selection = qdr_test::edge_selection({ source_edge }, source_edge);

	const OperationResult result = collapse_selected_edges(document, selection);

	REQUIRE(result.changed);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 1U);
	const Vertex* survivor = find_vertex(document, selection.vertices.front());
	REQUIRE(survivor != nullptr);
	REQUIRE(qdr_test::vec_nearly_equal(survivor->position, midpoint));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(qdr_test::every_edge_is_closed(document));
}

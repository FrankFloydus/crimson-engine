////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

#include <algorithm>

using namespace quader_poly;

TEST_CASE("connect vertices splits a shared quad face and selects the new edge", "[operation][connect_vertices]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left, fixture.top_right });

	const OperationResult result = connect_selected_vertices(fixture.document, selection);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.faces.size() == 2U);
	REQUIRE(selection.mode == SelectionMode::edge);
	REQUIRE(selection.edges.size() == 1U);
	REQUIRE(selection_contains(selection, make_edge(fixture.bottom_left, fixture.top_right)));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("connect vertices keeps cube cuts manifold for diagonal and multi-vertex selections", "[operation][connect_vertices]")
{
	SECTION("two non-adjacent cube face vertices create one closed diagonal")
	{
		Document document = make_cube({});
		const Face face = document.faces.front();
		Selection selection = qdr_test::vertex_selection({ face.vertices[0], face.vertices[2] });

		const OperationResult result = connect_selected_vertices(document, selection);

		REQUIRE(result.changed);
		REQUIRE(document.faces.size() == 7U);
		REQUIRE(qdr_test::document_has_edge(document, make_edge(face.vertices[0], face.vertices[2])));
		REQUIRE(selection.mode == SelectionMode::edge);
		REQUIRE(selection.edges.size() == 1U);
		REQUIRE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
		REQUIRE(qdr_test::every_edge_is_closed(document));
	}

	SECTION("three selected cube face vertices create the closing selected-vertex edge sequence")
	{
		Document document = make_cube({});
		const Face face = document.faces.front();
		Selection selection = qdr_test::vertex_selection({
			face.vertices[0],
			face.vertices[1],
			face.vertices[2],
		});

		const OperationResult result = connect_selected_vertices(document, selection);

		REQUIRE(result.changed);
		REQUIRE(document.faces.size() == 7U);
		REQUIRE(qdr_test::document_has_edge(document, make_edge(face.vertices[2], face.vertices[0])));
		REQUIRE(selection.mode == SelectionMode::edge);
		REQUIRE(selection.edges.size() == 3U);
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
		REQUIRE(qdr_test::every_edge_is_closed(document));
	}
}

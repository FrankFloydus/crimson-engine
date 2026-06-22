////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("bridge edge pairs creates faces between explicit edge pairs", "[operation][bridge_edge_pairs]")
{
	SECTION("one explicit pair bridges opposite open cube edges")
	{
		Document document = make_cube({});
		const Face removed_face = document.faces.front();
		document.faces.erase(document.faces.begin());
		Selection selection;
		const Edge first = make_edge(removed_face.vertices[0], removed_face.vertices[1]);
		const Edge second = make_edge(removed_face.vertices[2], removed_face.vertices[3]);

		const OperationResult result = bridge_edge_pairs(document, selection, { { first, second } }, 2);

		REQUIRE(result.changed);
		REQUIRE(selection.mode == SelectionMode::face);
		REQUIRE(selection.faces.size() == 2U);
		REQUIRE(qdr_test::edge_incidence_count(document, first) == 2);
		REQUIRE(qdr_test::edge_incidence_count(document, second) == 2);
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	}

	SECTION("one explicit pair bridges non-boundary cube edges")
	{
		Document document = make_cube({});
		const std::size_t face_count_before = document.faces.size();
		Selection selection;
		const Edge first = make_edge(document.faces.front().vertices[0], document.faces.front().vertices[1]);
		const Edge second = make_edge(document.faces.front().vertices[2], document.faces.front().vertices[3]);
		REQUIRE(qdr_test::edge_incidence_count(document, first) == 2);
		REQUIRE(qdr_test::edge_incidence_count(document, second) == 2);

		const OperationResult result = bridge_edge_pairs(document, selection, { { first, second } }, 1);

		REQUIRE(result.changed);
		REQUIRE(document.faces.size() == face_count_before + 1U);
		REQUIRE(selection.mode == SelectionMode::face);
		REQUIRE(selection.faces.size() == 1U);
		REQUIRE(qdr_test::edge_incidence_count(document, first) == 3);
		REQUIRE(qdr_test::edge_incidence_count(document, second) == 3);
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	}

	SECTION("empty pair list fails without mutating")
	{
		Document document = make_cube({});
		Selection selection;

		const OperationResult empty_result = bridge_edge_pairs(document, selection, {}, 1);
		REQUIRE_FALSE(empty_result.changed);
		REQUIRE(empty_result.message == "Bridge needs one or more selected edge pairs.");
	}
}

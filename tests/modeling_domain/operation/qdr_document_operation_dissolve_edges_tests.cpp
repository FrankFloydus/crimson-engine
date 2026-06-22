////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

#include <algorithm>

using namespace quader_poly;

TEST_CASE("dissolve edges removes a redundant support cut and restores one quad", "[operation][dissolve_edges]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection connect_selection = qdr_test::edge_selection({
		make_edge(fixture.bottom_left, fixture.bottom_right),
		make_edge(fixture.top_right, fixture.top_left),
	});
	REQUIRE(connect_selected_edges(fixture.document, connect_selection).changed);
	REQUIRE(connect_selection.edges.size() == 1U);

	Selection dissolve_selection = qdr_test::edge_selection({ connect_selection.edges.front() }, connect_selection.edges.front());
	const OperationResult result = dissolve_selected_edges(fixture.document, dissolve_selection);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.vertices.size() == 4U);
	REQUIRE(fixture.document.faces.size() == 1U);
	REQUIRE(fixture.document.faces.front().vertices.size() == 4U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("dissolve edges restores a shared cube cut without opening the hull", "[operation][dissolve_edges]")
{
	Document document = make_cube({});
	const Face face = document.faces.front();
	Selection connect_selection = qdr_test::edge_selection({
		make_edge(face.vertices[0], face.vertices[1]),
		make_edge(face.vertices[2], face.vertices[3]),
	});
	const std::size_t vertex_count_before = document.vertices.size();
	REQUIRE(connect_selected_edges(document, connect_selection).changed);
	const Edge cut_edge = connect_selection.edges.front();
	Selection dissolve_selection = qdr_test::edge_selection({ cut_edge }, cut_edge);

	const OperationResult result = dissolve_selected_edges(document, dissolve_selection);

	REQUIRE(result.changed);
	REQUIRE(document.vertices.size() == vertex_count_before);
	REQUIRE(find_vertex(document, cut_edge.a) == nullptr);
	REQUIRE(find_vertex(document, cut_edge.b) == nullptr);
	REQUIRE_FALSE(qdr_test::document_has_edge(document, cut_edge));
	REQUIRE(dissolve_selection.mode == SelectionMode::face);
	REQUIRE(dissolve_selection.faces.size() == 1U);
	REQUIRE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(qdr_test::every_edge_is_closed(document));
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

#include <algorithm>

using namespace quader_poly;

TEST_CASE("bevel vertices trims the source corner and selects generated replacements", "[operation][bevel_vertices]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left }, fixture.bottom_left);

	const OperationResult result = bevel_selected_vertices(fixture.document, selection, 0.25F);

	REQUIRE(result.changed);
	REQUIRE(find_vertex(fixture.document, fixture.bottom_left) == nullptr);
	REQUIRE(fixture.document.vertices.size() == 5U);
	REQUIRE(fixture.document.faces.size() == 1U);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 2U);
	for (ElementId vertex_id : selection.vertices) {
		REQUIRE(find_vertex(fixture.document, vertex_id) != nullptr);
	}
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("bevel vertices on a cube creates an outward cap and keeps the hull closed", "[operation][bevel_vertices]")
{
	Document document = make_cube({});
	const ElementId vertex_id = document.vertices.front().id;
	const Vec3 vertex_position = document.vertices.front().position;
	const std::size_t vertex_count_before = document.vertices.size();
	const std::size_t face_count_before = document.faces.size();
	Selection selection = qdr_test::vertex_selection({ vertex_id }, vertex_id);

	const OperationResult result = bevel_selected_vertices(document, selection, 16.0F);

	REQUIRE(result.changed);
	REQUIRE(document.vertices.size() == vertex_count_before + 2U);
	REQUIRE(document.faces.size() == face_count_before + 1U);
	REQUIRE(find_vertex(document, vertex_id) == nullptr);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 3U);
	for (ElementId split_vertex_id : selection.vertices) {
		const Vertex* split_vertex = find_vertex(document, split_vertex_id);
		REQUIRE(split_vertex != nullptr);
		const float split_distance = length(split_vertex->position - vertex_position);
		REQUIRE(split_distance > 0.0F);
		REQUIRE(split_distance <= 16.0F);
	}

	const auto cap_face = std::find_if(document.faces.begin(), document.faces.end(), [&selection](const Face& face) {
		return face.vertices.size() == selection.vertices.size() && qdr_test::face_contains_all_vertices(face, selection.vertices);
	});
	REQUIRE(cap_face != document.faces.end());
	REQUIRE(dot(face_normal(document, *cap_face), vertex_position - qdr_test::face_centroid(document, *cap_face)) > 0.0F);
	REQUIRE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(qdr_test::every_edge_is_closed(document));
}

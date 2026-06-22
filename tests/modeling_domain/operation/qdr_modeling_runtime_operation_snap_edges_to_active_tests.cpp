////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("snap edges to active aligns selected edge endpoints to the active edge", "[operation][snap_edges_to_active]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	quader_editor_core::PolygonObject& object = session.objects().front();
	const Face first_face = object.document.faces.front();
	const Edge active = make_edge(first_face.vertices[0], first_face.vertices[1]);
	const Edge source = make_edge(first_face.vertices[2], first_face.vertices[3]);
	quader_poly::Selection selection = qdr_test::edge_selection({ active, source }, active);
	session.set_component_selection(object.id, selection, quader_editor_core::SelectionMode::edge);

	const quader_editor_core::CommandResult result = session.snap_selected_edges_to_active();

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	const Vertex* active_a = find_vertex(session.objects().front().document, active.a);
	const Vertex* active_b = find_vertex(session.objects().front().document, active.b);
	const Vertex* source_a = find_vertex(session.objects().front().document, source.a);
	const Vertex* source_b = find_vertex(session.objects().front().document, source.b);
	REQUIRE(active_a != nullptr);
	REQUIRE(active_b != nullptr);
	REQUIRE(source_a != nullptr);
	REQUIRE(source_b != nullptr);
	const bool direct = qdr_test::vec_nearly_equal(source_a->position, active_a->position) &&
		qdr_test::vec_nearly_equal(source_b->position, active_b->position);
	const bool flipped = qdr_test::vec_nearly_equal(source_a->position, active_b->position) &&
		qdr_test::vec_nearly_equal(source_b->position, active_a->position);
	REQUIRE((direct || flipped));
}

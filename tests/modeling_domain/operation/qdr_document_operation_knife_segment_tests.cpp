////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("knife segment splits a quad between two boundary edge targets", "[operation][knife_segment]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Face* source_face = find_face(fixture.document, fixture.face);
	REQUIRE(source_face != nullptr);
	source_face->material_slot = 7;
	ensure_face_uvs(fixture.document);
	const ElementId bottom_right = fixture.bottom_right;
	const KnifePointTarget previous {
		KnifePointTargetKind::existing_edge,
		invalid_element_id,
		make_edge(fixture.bottom_left, fixture.bottom_right),
		0.25F,
		fixture.face,
		{},
	};
	const KnifePointTarget current {
		KnifePointTargetKind::existing_edge,
		invalid_element_id,
		make_edge(fixture.top_right, fixture.top_left),
		0.75F,
		fixture.face,
		{},
	};
	const KnifeSegmentPreview preview = build_knife_segment_preview(fixture.document, previous, current);
	REQUIRE(preview.valid);
	REQUIRE(preview.overlay.lines.size() == 1U);
	REQUIRE(qdr_test::vec_nearly_equal(preview.overlay.lines.front().start, { -0.5F, -1.0F, 0.0F }));
	REQUIRE(qdr_test::vec_nearly_equal(preview.overlay.lines.front().end, { -0.5F, 1.0F, 0.0F }));
	Selection selection;
	const std::size_t vertex_count_before = fixture.document.vertices.size();

	const OperationResult result = knife_segment(fixture.document, selection, previous, current);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.vertices.size() == vertex_count_before + 2U);
	REQUIRE(fixture.document.faces.size() == 2U);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 2U);
	REQUIRE(selection.has_active);
	REQUIRE(selection.active_kind == ElementKind::vertex);
	REQUIRE(selection.active_vertex == selection.vertices.back());
	REQUIRE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[0], selection.vertices[1])));
	REQUIRE(qdr_test::edge_positions_match_unordered(fixture.document, make_edge(selection.vertices[0], selection.vertices[1]), { -0.5F, -1.0F, 0.0F }, { -0.5F, 1.0F, 0.0F }));
	for (const Face& face : fixture.document.faces) {
		REQUIRE(face.material_slot == 7U);
		REQUIRE(face.uvs.empty());
	}
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));

	const KnifePointTarget inserted_previous {
		KnifePointTargetKind::inserted_vertex,
		selection.active_vertex,
		{},
		0.5F,
		invalid_element_id,
		{},
	};
	const KnifePointTarget existing_vertex_current {
		KnifePointTargetKind::existing_vertex,
		bottom_right,
		{},
		0.5F,
		invalid_element_id,
		{},
	};
	const OperationResult continued_result = knife_segment(fixture.document, selection, inserted_previous, existing_vertex_current);
	REQUIRE(continued_result.changed);
	REQUIRE(fixture.document.faces.size() == 3U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("knife segment rejects two targets on the same source edge without mutation", "[operation][knife_segment]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::size_t vertices_before = fixture.document.vertices.size();
	const std::size_t faces_before = fixture.document.faces.size();
	const Edge edge = make_edge(fixture.bottom_left, fixture.bottom_right);
	const KnifePointTarget previous { KnifePointTargetKind::existing_edge, invalid_element_id, edge, 0.25F, fixture.face, {} };
	const KnifePointTarget current { KnifePointTargetKind::existing_edge, invalid_element_id, edge, 0.75F, fixture.face, {} };
	Selection selection;

	const OperationResult result = knife_segment(fixture.document, selection, previous, current);

	REQUIRE_FALSE(result.changed);
	REQUIRE(fixture.document.vertices.size() == vertices_before);
	REQUIRE(fixture.document.faces.size() == faces_before);
}

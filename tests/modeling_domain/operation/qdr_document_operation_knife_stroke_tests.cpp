////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("knife stroke applies connected stroke segments in one operation", "[operation][knife_stroke]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::vector<KnifePointTarget> points {
		{
			KnifePointTargetKind::existing_edge,
			invalid_element_id,
			make_edge(fixture.bottom_left, fixture.bottom_right),
			0.5F,
			fixture.face,
			{},
		},
		{
			KnifePointTargetKind::face_point,
			invalid_element_id,
			{},
			0.5F,
			fixture.face,
			{ 0.0F, 0.0F, 0.0F },
		},
		{
			KnifePointTargetKind::existing_edge,
			invalid_element_id,
			make_edge(fixture.top_right, fixture.top_left),
			0.5F,
			fixture.face,
			{},
		},
	};
	const std::vector<KnifeStrokeSegment> segments {
		{ 0U, 1U },
		{ 1U, 2U },
	};
	Selection selection;

	const OperationResult result = knife_stroke(fixture.document, selection, points, segments);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.faces.size() == 2U);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 3U);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("knife stroke supports two split points on the same source edge", "[operation][knife_stroke]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::vector<KnifePointTarget> points {
		{
			KnifePointTargetKind::existing_edge,
			invalid_element_id,
			make_edge(fixture.bottom_left, fixture.bottom_right),
			0.25F,
			fixture.face,
			{ -0.5F, -1.0F, 0.0F },
		},
		{
			KnifePointTargetKind::face_point,
			invalid_element_id,
			{},
			0.5F,
			fixture.face,
			{ 0.0F, -0.25F, 0.0F },
		},
		{
			KnifePointTargetKind::existing_edge,
			invalid_element_id,
			Edge { fixture.bottom_right, fixture.bottom_left },
			0.25F,
			fixture.face,
			{ 0.5F, -1.0F, 0.0F },
		},
	};
	const std::vector<KnifeStrokeSegment> segments {
		{ 0U, 1U },
		{ 1U, 2U },
	};
	Selection selection;

	const OperationResult result = knife_stroke(fixture.document, selection, points, segments);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.faces.size() == 2U);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 3U);
	REQUIRE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[0], selection.vertices[1])));
	REQUIRE(qdr_test::document_has_edge(fixture.document, make_edge(selection.vertices[1], selection.vertices[2])));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("knife stroke routes adjacent face cuts through the shared boundary", "[operation][knife_stroke]")
{
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const ElementId bottom_center = add_vertex(document, { 0.0F, -1.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const ElementId top_center = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId left_face = add_face(document, std::array<ElementId, 4> { bottom_left, bottom_center, top_center, top_left });
	const ElementId right_face = add_face(document, std::array<ElementId, 4> { bottom_center, bottom_right, top_right, top_center });
	REQUIRE(left_face != invalid_element_id);
	REQUIRE(right_face != invalid_element_id);

	const std::vector<KnifePointTarget> points {
		{
			KnifePointTargetKind::face_point,
			invalid_element_id,
			{},
			0.5F,
			left_face,
			{ -0.5F, 0.0F, 0.0F },
		},
		{
			KnifePointTargetKind::face_point,
			invalid_element_id,
			{},
			0.5F,
			right_face,
			{ 0.5F, 0.0F, 0.0F },
		},
	};
	const std::vector<KnifeStrokeSegment> segments { { 0U, 1U } };
	Selection selection;

	const OperationResult result = knife_stroke(document, selection, points, segments);

	REQUIRE(result.changed);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 2U);
	const auto boundary = std::find_if(document.vertices.begin(), document.vertices.end(), [](const Vertex& vertex) {
		return qdr_test::vec_nearly_equal(vertex.position, { 0.0F, 0.0F, 0.0F });
	});
	const ElementId boundary_vertex = boundary != document.vertices.end() ? boundary->id : invalid_element_id;
	REQUIRE(boundary_vertex != invalid_element_id);
	REQUIRE(qdr_test::document_has_edge(document, make_edge(selection.vertices[0], boundary_vertex)));
	REQUIRE(qdr_test::document_has_edge(document, make_edge(boundary_vertex, selection.vertices[1])));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST_CASE("knife stroke rejects incomplete strokes without mutation", "[operation][knife_stroke]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	const std::size_t vertex_count_before = fixture.document.vertices.size();
	const std::size_t face_count_before = fixture.document.faces.size();
	Selection selection;

	const OperationResult result = knife_stroke(fixture.document, selection, {}, {});

	REQUIRE_FALSE(result.changed);
	REQUIRE(fixture.document.vertices.size() == vertex_count_before);
	REQUIRE(fixture.document.faces.size() == face_count_before);
}

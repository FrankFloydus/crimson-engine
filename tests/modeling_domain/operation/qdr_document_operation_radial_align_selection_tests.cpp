////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

using namespace quader_poly;

TEST_CASE("radial align selection moves an uneven vertex ring onto a shared radius", "[operation][radial_align_selection]")
{
	Document document;
	const ElementId a = add_vertex(document, { -2.0F, 0.0F, 0.0F });
	const ElementId b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId c = add_vertex(document, { 3.0F, 0.0F, 0.0F });
	const ElementId d = add_vertex(document, { 0.0F, -1.0F, 0.0F });
	Selection selection = qdr_test::vertex_selection({ a, b, c, d });

	const OperationResult result = radial_align_selection(document, selection);

	REQUIRE(result.changed);
	const Vec3 center = qdr_test::document_vertex_centroid(document);
	std::optional<float> radius;
	for (ElementId vertex_id : selection.vertices) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		REQUIRE(vertex != nullptr);
		const float distance = length(vertex->position - center);
		if (!radius.has_value()) {
			radius = distance;
		}
		REQUIRE(distance == Catch::Approx(*radius).margin(0.001F));
	}
}

TEST_CASE("radial align selection preserves edge and face selection ownership", "[operation][radial_align_selection]")
{
	SECTION("edge mode aligns all vertices referenced by selected edges")
	{
		Document document;
		const ElementId a = add_vertex(document, { -2.0F, 0.0F, 0.0F });
		const ElementId b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
		const ElementId c = add_vertex(document, { 3.0F, 0.0F, 0.0F });
		const ElementId d = add_vertex(document, { 0.0F, -1.0F, 0.0F });
		REQUIRE(add_face(document, std::array<ElementId, 4> { a, b, c, d }) != invalid_element_id);
		Selection selection = qdr_test::edge_selection({ make_edge(a, b), make_edge(b, c), make_edge(c, d), make_edge(d, a) });

		const OperationResult result = radial_align_selection(document, selection);

		REQUIRE(result.changed);
		REQUIRE(selection.mode == SelectionMode::edge);
		REQUIRE(selection.edges.size() == 4U);
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	}

	SECTION("face mode aligns all vertices from the selected face loop")
	{
		Document document;
		const ElementId a = add_vertex(document, { -2.0F, 0.0F, 0.0F });
		const ElementId b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
		const ElementId c = add_vertex(document, { 3.0F, 0.0F, 0.0F });
		const ElementId d = add_vertex(document, { 0.0F, -1.0F, 0.0F });
		const ElementId face = add_face(document, std::array<ElementId, 4> { a, b, c, d });
		REQUIRE(face != invalid_element_id);
		Selection selection = qdr_test::face_selection({ face }, face);

		const OperationResult result = radial_align_selection(document, selection);

		REQUIRE(result.changed);
		REQUIRE(selection.mode == SelectionMode::face);
		REQUIRE(selection.faces.size() == 1U);
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	}
}

TEST_CASE("radial align selection keeps interior patch vertices off the perimeter", "[operation][radial_align_selection]")
{
	Document document;
	const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const ElementId bottom = add_vertex(document, { 0.0F, -1.0F, 0.0F });
	const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const ElementId left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	const ElementId center = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const ElementId right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const ElementId top = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
	REQUIRE(add_face(document, std::array<ElementId, 4> { bottom_left, bottom, center, left }) != invalid_element_id);
	REQUIRE(add_face(document, std::array<ElementId, 4> { bottom, bottom_right, right, center }) != invalid_element_id);
	REQUIRE(add_face(document, std::array<ElementId, 4> { left, center, top, top_left }) != invalid_element_id);
	REQUIRE(add_face(document, std::array<ElementId, 4> { center, right, top_right, top }) != invalid_element_id);
	Selection selection = qdr_test::vertex_selection({
		bottom_left,
		bottom,
		bottom_right,
		left,
		center,
		right,
		top_left,
		top,
		top_right,
	});

	const OperationResult result = radial_align_selection(document, selection);

	REQUIRE(result.changed);
	const Vertex* center_vertex = find_vertex(document, center);
	REQUIRE(center_vertex != nullptr);
	REQUIRE(qdr_test::vec_nearly_equal(center_vertex->position, { 0.0F, 0.0F, 0.0F }));

	std::optional<float> radius;
	for (const ElementId vertex_id : { bottom_left, bottom, bottom_right, left, right, top_left, top, top_right }) {
		const Vertex* vertex = find_vertex(document, vertex_id);
		REQUIRE(vertex != nullptr);
		const float distance = length(vertex->position - center_vertex->position);
		REQUIRE(distance > 0.1F);
		if (!radius.has_value()) {
			radius = distance;
		}
		REQUIRE(distance == Catch::Approx(*radius).margin(0.001F));
	}
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}

TEST_CASE("radial align selection keeps a square face island cardinally phased", "[operation][radial_align_selection]")
{
	Document document;
	std::array<std::array<ElementId, 4>, 4> vertices {};
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			vertices[y][x] = add_vertex(document, {
				static_cast<float>(x) - 1.5F,
				static_cast<float>(y) - 1.5F,
				0.0F,
			});
		}
	}

	Selection selection;
	selection.mode = SelectionMode::face;
	for (int y = 0; y < 3; ++y) {
		for (int x = 0; x < 3; ++x) {
			const ElementId face = add_face(document, std::array<ElementId, 4> {
				vertices[y][x],
				vertices[y][x + 1],
				vertices[y + 1][x + 1],
				vertices[y + 1][x],
			});
			REQUIRE(face != invalid_element_id);
			selection.faces.push_back(face);
		}
	}
	selection.has_active = true;
	selection.active_kind = ElementKind::face;
	selection.active_face = selection.faces.back();

	const OperationResult result = radial_align_selection(document, selection);

	REQUIRE(result.changed);
	constexpr float expected_step_degrees = 30.0F;
	constexpr float expected_phase_degrees = 15.0F;
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 4; ++x) {
			if (x > 0 && x < 3 && y > 0 && y < 3) {
				continue;
			}
			const Vertex* vertex = find_vertex(document, vertices[y][x]);
			REQUIRE(vertex != nullptr);
			const float angle_degrees = std::atan2(vertex->position.y, vertex->position.x) * 180.0F / static_cast<float>(std::acos(-1.0));
			const float nearest_step = expected_phase_degrees +
				(std::round((angle_degrees - expected_phase_degrees) / expected_step_degrees) * expected_step_degrees);
			REQUIRE(angle_degrees == Catch::Approx(nearest_step).margin(0.01F));
		}
	}
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
}

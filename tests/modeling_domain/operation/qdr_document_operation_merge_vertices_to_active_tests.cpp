////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace quader_poly;

TEST_CASE("merge vertices to active preserves active vertex and rewires faces", "[operation][merge_vertices_to_active]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right }, fixture.bottom_left);

	const OperationResult result = merge_selected_vertices_to_active(fixture.document, selection);

	REQUIRE(result.changed);
	REQUIRE(find_vertex(fixture.document, fixture.bottom_left) != nullptr);
	REQUIRE(find_vertex(fixture.document, fixture.bottom_right) == nullptr);
	REQUIRE(selection.mode == SelectionMode::vertex);
	REQUIRE(selection.vertices.size() == 1U);
	REQUIRE(selection.vertices.front() == fixture.bottom_left);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("merge vertices to active preserves cube topology and sloped UV loops", "[operation][merge_vertices_to_active]")
{
	SECTION("adjacent cube vertices collapse into the active id without opening the hull")
	{
		Document document = make_cube({});
		const Edge edge = document_edges(document).front();
		const std::size_t vertex_count_before = document.vertices.size();
		Selection selection = qdr_test::vertex_selection({ edge.a, edge.b }, edge.a);

		const OperationResult result = merge_selected_vertices_to_active(document, selection);

		REQUIRE(result.changed);
		REQUIRE(document.vertices.size() == vertex_count_before - 1U);
		REQUIRE(find_vertex(document, edge.a) != nullptr);
		REQUIRE(find_vertex(document, edge.b) == nullptr);
		REQUIRE(selection.has_active);
		REQUIRE(selection.vertices.size() == 1U);
		REQUIRE(selection.active_vertex == edge.a);
		REQUIRE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
		REQUIRE(qdr_test::every_edge_is_closed(document));
	}

	SECTION("diagonal same-face merge bakes source-axis UVs onto generated slopes")
	{
		Document document = make_cube({});
		const Face source_face = document.faces.front();
		const ElementId active_vertex = source_face.vertices[0];
		const ElementId source_vertex = source_face.vertices[2];
		const std::size_t vertex_count_before = document.vertices.size();
		Selection selection = qdr_test::vertex_selection({ active_vertex, source_vertex }, active_vertex);

		const OperationResult result = merge_selected_vertices_to_active(document, selection);

		REQUIRE(result.changed);
		REQUIRE(document.vertices.size() == vertex_count_before - 1U);
		REQUIRE(find_vertex(document, active_vertex) != nullptr);
		REQUIRE(find_vertex(document, source_vertex) == nullptr);
		REQUIRE(std::none_of(document.faces.begin(), document.faces.end(), qdr_test::face_has_repeated_vertices));
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));

		const CompiledMesh mesh = compile_document(document);
		std::size_t expected_compiled_vertices = 0;
		for (const Face& face : document.faces) {
			expected_compiled_vertices += face.vertices.size();
		}
		REQUIRE(mesh.vertices.size() == expected_compiled_vertices);

		bool checked_sloped_uv = false;
		std::size_t compiled_vertex_offset = 0;
		for (const Face& face : document.faces) {
			const quader::QVec3 normal = face_normal(document, face);
			if (std::abs(normal.y) > 0.25F && qdr_test::face_has_vertical_span(document, face)) {
				REQUIRE(face.uvs.size() == face.vertices.size());
				for (std::size_t index = 0; index < face.vertices.size(); ++index) {
					REQUIRE(qdr_test::vec2_nearly_equal(mesh.vertices[compiled_vertex_offset + index].uv0, face.uvs[index], 0.001F));
				}
				checked_sloped_uv = true;
				break;
			}
			compiled_vertex_offset += face.vertices.size();
		}
		REQUIRE(checked_sloped_uv);
	}

	SECTION("sequential ramp merges keep the diagonal seam and baked sloped UVs")
	{
		Document document = make_cube({});
		const ElementId top_front_left = 4;
		const ElementId top_front_right = 3;
		const ElementId top_back_right = 7;
		const ElementId top_back_left = 6;

		Selection first = qdr_test::vertex_selection({ top_front_left, top_front_right }, top_front_right);
		REQUIRE(merge_selected_vertices_to_active(document, first).changed);
		REQUIRE(qdr_test::document_has_edge(document, make_edge(top_front_right, top_back_left)));

		Selection second = qdr_test::vertex_selection({ top_back_right, top_back_left }, top_back_left);
		REQUIRE(merge_selected_vertices_to_active(document, second).changed);
		REQUIRE(qdr_test::document_has_edge(document, make_edge(top_front_right, top_back_left)));

		bool checked_sloped_uv = false;
		for (const Face& face : document.faces) {
			const quader::QVec3 normal = face_normal(document, face);
			if (std::abs(normal.y) > 0.25F &&
				qdr_test::face_has_vertical_span(document, face) &&
				face.uvs.size() == face.vertices.size()) {
				checked_sloped_uv = true;
				break;
			}
		}
		REQUIRE(checked_sloped_uv);
		REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
		REQUIRE(qdr_test::every_edge_is_closed(document));
	}
}

TEST_CASE("merge vertices to active collapses opposite top cube corners to the active corner", "[operation][merge_vertices_to_active]")
{
	Document document = make_cube({});
	const ElementId bottom_front_left = 1;
	const ElementId bottom_front_right = 2;
	const ElementId top_front_right = 3;
	const ElementId top_front_left = 4;
	const ElementId bottom_back_left = 5;
	const ElementId top_back_left = 6;
	const ElementId top_back_right = 7;
	const ElementId bottom_back_right = 8;

	Selection selection = qdr_test::vertex_selection({ top_front_left, top_back_right }, top_back_right);

	const OperationResult result = merge_selected_vertices_to_active(document, selection);

	const auto has_face = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return std::any_of(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return face.vertices.size() == expected.size() && qdr_test::face_contains_all_vertices(face, expected);
		});
	};
	const auto has_cyclic_face = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return std::any_of(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return qdr_test::face_vertices_match_cyclic(face, expected);
		});
	};
	const auto face_count = [&document](std::initializer_list<ElementId> vertex_ids) {
		const std::vector<ElementId> expected(vertex_ids.begin(), vertex_ids.end());
		return static_cast<std::size_t>(std::count_if(document.faces.begin(), document.faces.end(), [&expected](const Face& face) {
			return face.vertices.size() == expected.size() && qdr_test::face_contains_all_vertices(face, expected);
		}));
	};
	REQUIRE(result.changed);
	REQUIRE(find_vertex(document, top_front_left) == nullptr);
	const Vertex* active = find_vertex(document, top_back_right);
	REQUIRE(active != nullptr);
	REQUIRE(qdr_test::vec_nearly_equal(active->position, { 1.0F, 1.0F, -1.0F }));
	REQUIRE_FALSE(has_face({ bottom_back_left, bottom_front_left, bottom_front_right, top_front_right, top_back_right, top_back_left }));
	REQUIRE(has_cyclic_face({ bottom_front_left, bottom_front_right, top_front_right, top_back_right }));
	REQUIRE(has_cyclic_face({ bottom_back_left, bottom_front_left, top_back_right, top_back_left }));
	REQUIRE(has_cyclic_face({ bottom_back_right, top_back_right, top_front_right, bottom_front_right }));
	REQUIRE(has_cyclic_face({ bottom_back_left, top_back_left, top_back_right, bottom_back_right }));
	REQUIRE_FALSE(qdr_test::document_has_edge(document, make_edge(top_back_left, top_front_right)));
	REQUIRE(qdr_test::edge_incidence_count(document, make_edge(bottom_front_left, top_back_right)) == 2);
	REQUIRE(face_count({ top_back_left, top_front_right, top_back_right }) == 0U);
	REQUIRE(document.faces.size() == 5U);
	REQUIRE(selection.has_active);
	REQUIRE(selection.vertices.size() == 1U);
	REQUIRE(selection.active_vertex == top_back_right);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(document));
	REQUIRE(qdr_test::every_edge_is_closed(document));
	const CompiledMesh mesh = compile_document(document);
	REQUIRE(mesh.primitives.size() == document.faces.size());
	REQUIRE(std::all_of(mesh.primitives.begin(), mesh.primitives.end(), [](const CompiledPrimitive& primitive) {
		return primitive.index_count == 6U;
	}));
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/modeling/operations/operation_schema.h>

#include <algorithm>
#include <limits>

using namespace quader_editor_core;

namespace {

bool positions_match(quader::QVec3 left, quader::QVec3 right)
{
	return qdr_test::vec_nearly_equal(left, right);
}

void translate_document(quader_poly::Document& document, quader::QVec3 delta)
{
	for (quader_poly::Vertex& vertex : document.vertices) {
		vertex.position += delta;
	}
}

quader_poly::Document make_distance_merge_document(float offset)
{
	quader_poly::Document document;
	const quader_poly::ElementId a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const quader_poly::ElementId b = add_vertex(document, { 1.0F, 0.0F, 0.0F });
	const quader_poly::ElementId c = add_vertex(document, { 0.0F, 1.0F, 0.0F });
	const quader_poly::ElementId d = add_vertex(document, { 0.0F, 1.0F + offset, 0.0F });
	const quader_poly::ElementId e = add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId f = add_vertex(document, { -1.0F, 0.0F, 0.0F });
	REQUIRE(add_face(document, std::array<quader_poly::ElementId, 3> { a, b, c }) != quader_poly::invalid_element_id);
	REQUIRE(add_face(document, std::array<quader_poly::ElementId, 3> { d, e, f }) != quader_poly::invalid_element_id);
	return document;
}

quader_poly::Edge edge_between_positions(
		const quader_poly::Document& document,
		quader::QVec3 first_position,
		quader::QVec3 second_position)
{
	for (const quader_poly::Edge edge : quader_poly::document_edges(document)) {
		const quader_poly::Vertex* first = quader_poly::find_vertex(document, edge.a);
		const quader_poly::Vertex* second = quader_poly::find_vertex(document, edge.b);
		if (first == nullptr || second == nullptr) {
			continue;
		}

		const bool direct =
			positions_match(first->position, first_position) &&
			positions_match(second->position, second_position);
		const bool reversed =
			positions_match(first->position, second_position) &&
			positions_match(second->position, first_position);
		if (direct || reversed) {
			return edge;
		}
	}
	return {};
}

std::vector<quader_poly::ElementId> vertices_at_positions(
		const quader_poly::Document& document,
		std::initializer_list<quader::QVec3> positions)
{
	std::vector<quader_poly::ElementId> vertices;
	for (const quader_poly::Vertex& vertex : document.vertices) {
		if (std::ranges::any_of(positions, [&vertex](quader::QVec3 position) {
			return positions_match(vertex.position, position);
		})) {
			vertices.push_back(vertex.id);
		}
	}
	return vertices;
}

quader_poly::Selection near_vertex_selection(const quader_poly::Document& document)
{
	REQUIRE(document.vertices.size() >= 4U);
	return qdr_test::vertex_selection({ document.vertices[2].id, document.vertices[3].id });
}

} // namespace

TEST_CASE("merge by distance operation schema owns tolerance defaults and limits", "[operation][merge_by_distance]")
{
	const MergeByDistanceOperationSchema schema = merge_by_distance_operation_schema();

	REQUIRE(schema.id == EditorOperationId::merge_by_distance);
	REQUIRE(std::string(schema.operation_id) == "merge_by_distance");
	REQUIRE(std::string(schema.label) == "Merge by Distance");
	REQUIRE(std::string(schema.tolerance.id) == "tolerance");
	REQUIRE(schema.tolerance.default_value == Catch::Approx(0.125));
	REQUIRE(schema.tolerance.minimum == Catch::Approx(0.0));
	REQUIRE(schema.tolerance.maximum == Catch::Approx(128.0));
	REQUIRE(schema.tolerance.step == Catch::Approx(0.001));
	REQUIRE(schema.tolerance.hard_limits);

	REQUIRE(normalize_merge_by_distance_operation_settings({ -1.0 }).tolerance == Catch::Approx(0.0));
	REQUIRE(normalize_merge_by_distance_operation_settings({ 1000.0 }).tolerance == Catch::Approx(128.0));
	REQUIRE(normalize_merge_by_distance_operation_settings({ std::numeric_limits<double>::infinity() }).tolerance == Catch::Approx(0.125));
}

TEST_CASE("merge by distance preview rebuilds from base and commits transactionally", "[operation][merge_by_distance]")
{
	SECTION("update and cancel restore the original selected mesh")
	{
		quader_poly::Document document = make_distance_merge_document(0.2F);
		EditorSession session({ qdr_test::make_object(1, document) });
		session.set_component_selection(1, near_vertex_selection(session.objects().front().document), SelectionMode::vertex);
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		const CommandResult begin = session.begin_merge_by_distance_preview(0.05);
		REQUIRE(begin.ok);
		REQUIRE_FALSE(begin.changed);
		REQUIRE(session.preview_transaction_status().active);
		REQUIRE(session.preview_transaction_status().kind == PreviewTransactionKind::merge_by_distance);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before);

		REQUIRE(session.update_merge_by_distance_preview(0.25).changed);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);

		const PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(PreviewTransactionKind::merge_by_distance);

		REQUIRE(cancel.restored);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before);
		REQUIRE_FALSE(session.history_status().can_undo);
	}

	SECTION("commit records the preview and undo restores the base")
	{
		quader_poly::Document document = make_distance_merge_document(0.1F);
		EditorSession session({ qdr_test::make_object(1, document) });
		session.set_component_selection(1, near_vertex_selection(session.objects().front().document), SelectionMode::vertex);
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		REQUIRE(session.begin_merge_by_distance_preview({ 0.125 }).changed);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);
		const PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(PreviewTransactionKind::merge_by_distance);

		REQUIRE(commit.before.changed);
		REQUIRE(session.history_status().undo_count == 1U);
		REQUIRE(session.undo().applied);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before);
	}
}

TEST_CASE("merge by distance processes selected vertices within each mesh only", "[operation][merge_by_distance]")
{
	EditorSession session({
		qdr_test::make_object(1, make_distance_merge_document(0.1F)),
		qdr_test::make_object(2, make_distance_merge_document(0.1F)),
	});
	for (quader_editor_core::PolygonObject& object : session.objects()) {
		object.selected = true;
		object.selection = near_vertex_selection(object.document);
		object.selection.mode = quader_poly::SelectionMode::vertex;
	}
	session.state().selection_mode = SelectionMode::vertex;
	session.state().active_object_id = 1;
	const std::size_t first_before = session.objects()[0].document.vertices.size();
	const std::size_t second_before = session.objects()[1].document.vertices.size();

	const CommandResult result = session.begin_merge_by_distance_preview(0.125);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(session.objects().size() == 2U);
	REQUIRE(session.objects()[0].document.vertices.size() == first_before - 1U);
	REQUIRE(session.objects()[1].document.vertices.size() == second_before - 1U);
}

TEST_CASE("merge by distance keeps a combined welded seam usable by edge loop", "[operation][merge_by_distance][combine_mesh][insert_edge_loop]")
{
	quader_poly::Document left = qdr_test::make_quad_document().document;
	quader_poly::Document right = qdr_test::make_quad_document().document;
	translate_document(right, { 2.0F, 0.0F, 0.0F });
	EditorSession session({ qdr_test::make_object(1, left), qdr_test::make_object(2, right) });
	session.set_selected_object_ids({ 1, 2 }, 1);
	REQUIRE(session.combine_selected_meshes().ok);
	REQUIRE(session.objects().size() == 1U);

	const std::vector<quader_poly::ElementId> seam_vertices = vertices_at_positions(
		session.objects().front().document,
		{ { 1.0F, -1.0F, 0.0F }, { 1.0F, 1.0F, 0.0F } });
	REQUIRE(seam_vertices.size() == 4U);
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::vertex;
	selection.vertices = seam_vertices;
	session.set_component_selection(1, selection, SelectionMode::vertex);

	REQUIRE(session.begin_merge_by_distance_preview(0.001).changed);
	const PreviewTransactionFinishResult merge_commit =
		session.commit_preview_transaction(PreviewTransactionKind::merge_by_distance);
	REQUIRE(merge_commit.before.active);
	REQUIRE(session.objects().front().document.vertices.size() == 6U);

	const quader_poly::Edge seed = edge_between_positions(
		session.objects().front().document,
		{ -1.0F, -1.0F, 0.0F },
		{ 1.0F, -1.0F, 0.0F });
	REQUIRE(seed.a != quader_poly::invalid_element_id);
	REQUIRE(seed.b != quader_poly::invalid_element_id);

	const CommandResult edge_loop = session.insert_edge_loop(1, seed, 0.5F);
	REQUIRE(edge_loop.ok);
	REQUIRE(edge_loop.changed);
	REQUIRE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
}

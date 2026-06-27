////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_schema.hpp>

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
	EXPECT_TRUE(add_face(document, std::array<quader_poly::ElementId, 3> { a, b, c }) != quader_poly::invalid_element_id);
	EXPECT_TRUE(add_face(document, std::array<quader_poly::ElementId, 3> { d, e, f }) != quader_poly::invalid_element_id);
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
	EXPECT_TRUE(document.vertices.size() >= 4U);
	return qdr_test::vertex_selection({ document.vertices[2].id, document.vertices[3].id });
}

} // namespace

TEST(QdrModelingRuntimeOperationMergeByDistanceTests, MergeByDistanceOperationSchemaOwnsToleranceDefaultsAndLimits) {
	const MergeByDistanceOperationSchema schema = merge_by_distance_operation_schema();

	EXPECT_TRUE(schema.id == EditorOperationId::merge_by_distance);
	EXPECT_TRUE(std::string(schema.operation_id) == "merge_by_distance");
	EXPECT_TRUE(std::string(schema.label) == "Merge by Distance");
	EXPECT_TRUE(std::string(schema.tolerance.id) == "tolerance");
	EXPECT_NEAR(schema.tolerance.default_value, 0.125, 0.0001);
	EXPECT_NEAR(schema.tolerance.minimum, 0.0, 0.0001);
	EXPECT_NEAR(schema.tolerance.maximum, 128.0, 0.0001);
	EXPECT_NEAR(schema.tolerance.step, 0.001, 0.0001);
	EXPECT_TRUE(schema.tolerance.hard_limits);

	EXPECT_NEAR(normalize_merge_by_distance_operation_settings({ -1.0 }).tolerance, 0.0, 0.0001);
	EXPECT_NEAR(normalize_merge_by_distance_operation_settings({ 1000.0 }).tolerance, 128.0, 0.0001);
	EXPECT_NEAR(normalize_merge_by_distance_operation_settings({ std::numeric_limits<double>::infinity() }).tolerance, 0.125, 0.0001);
}

TEST(QdrModelingRuntimeOperationMergeByDistanceTests, MergeByDistancePreviewRebuildsFromBaseAndCommitsTransactionallyUpdateAndCancelRestoreTheOriginalSelectedMesh) {
	{
		quader_poly::Document document = make_distance_merge_document(0.2F);
		EditorSession session({ qdr_test::make_object(1, document) });
		session.set_component_selection(1, near_vertex_selection(session.objects().front().document), SelectionMode::vertex);
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		const CommandResult begin = session.begin_merge_by_distance_preview(0.05);
		EXPECT_TRUE(begin.ok);
		EXPECT_FALSE(begin.changed);
		EXPECT_TRUE(session.preview_transaction_status().active);
		EXPECT_TRUE(session.preview_transaction_status().kind == PreviewTransactionKind::merge_by_distance);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before);

		EXPECT_TRUE(session.update_merge_by_distance_preview(0.25).changed);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);

		const PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(PreviewTransactionKind::merge_by_distance);

		EXPECT_TRUE(cancel.restored);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before);
		EXPECT_FALSE(session.history_status().can_undo);
	}

	
}

TEST(QdrModelingRuntimeOperationMergeByDistanceTests, MergeByDistancePreviewRebuildsFromBaseAndCommitsTransactionallyCommitRecordsThePreviewAndUndoRestoresTheBase) {
	

	{
		quader_poly::Document document = make_distance_merge_document(0.1F);
		EditorSession session({ qdr_test::make_object(1, document) });
		session.set_component_selection(1, near_vertex_selection(session.objects().front().document), SelectionMode::vertex);
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		EXPECT_TRUE(session.begin_merge_by_distance_preview({ 0.125 }).changed);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);
		const PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(PreviewTransactionKind::merge_by_distance);

		EXPECT_TRUE(commit.before.changed);
		EXPECT_TRUE(session.history_status().undo_count == 1U);
		EXPECT_TRUE(session.undo().applied);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before);
	}
}

TEST(QdrModelingRuntimeOperationMergeByDistanceTests, MergeByDistanceProcessesSelectedVerticesWithinEachMeshOnly) {
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

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	EXPECT_TRUE(session.objects()[0].document.vertices.size() == first_before - 1U);
	EXPECT_TRUE(session.objects()[1].document.vertices.size() == second_before - 1U);
}

TEST(QdrModelingRuntimeOperationMergeByDistanceTests, MergeByDistanceKeepsACombinedWeldedSeamUsableByEdgeLoop) {
	quader_poly::Document left = qdr_test::make_quad_document().document;
	quader_poly::Document right = qdr_test::make_quad_document().document;
	translate_document(right, { 2.0F, 0.0F, 0.0F });
	EditorSession session({ qdr_test::make_object(1, left), qdr_test::make_object(2, right) });
	session.set_selected_object_ids({ 1, 2 }, 1);
	EXPECT_TRUE(session.combine_selected_meshes().ok);
	EXPECT_TRUE(session.objects().size() == 1U);

	const std::vector<quader_poly::ElementId> seam_vertices = vertices_at_positions(
		session.objects().front().document,
		{ { 1.0F, -1.0F, 0.0F }, { 1.0F, 1.0F, 0.0F } });
	EXPECT_TRUE(seam_vertices.size() == 4U);
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::vertex;
	selection.vertices = seam_vertices;
	session.set_component_selection(1, selection, SelectionMode::vertex);

	EXPECT_TRUE(session.begin_merge_by_distance_preview(0.001).changed);
	const PreviewTransactionFinishResult merge_commit =
		session.commit_preview_transaction(PreviewTransactionKind::merge_by_distance);
	EXPECT_TRUE(merge_commit.before.active);
	EXPECT_TRUE(session.objects().front().document.vertices.size() == 6U);

	const quader_poly::Edge seed = edge_between_positions(
		session.objects().front().document,
		{ -1.0F, -1.0F, 0.0F },
		{ 1.0F, -1.0F, 0.0F });
	EXPECT_TRUE(seed.a != quader_poly::invalid_element_id);
	EXPECT_TRUE(seed.b != quader_poly::invalid_element_id);

	const CommandResult edge_loop = session.insert_edge_loop(1, seed, 0.5F);
	EXPECT_TRUE(edge_loop.ok);
	EXPECT_TRUE(edge_loop.changed);
	EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(session.objects().front().document));
}

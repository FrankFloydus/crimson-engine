////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_command.hpp>

namespace qec = quader_editor_core;

namespace {

qec::EditorOperationCommand selection_execute_command(qec::EditorOperationId operation_id)
{
	qec::EditorOperationCommand command;
	command.operation_id = operation_id;
	command.phase = qec::EditorOperationCommandPhase::execute;
	command.settings = qec::SelectionOperationSettings {};
	return command;
}

qec::EditorOperationSettings matching_settings(qec::EditorOperationSettingsKind kind)
{
	switch (kind) {
	case qec::EditorOperationSettingsKind::selection:
		return qec::SelectionOperationSettings {};
	case qec::EditorOperationSettingsKind::bridge:
		return qec::BridgeOperationSettings {};
	case qec::EditorOperationSettingsKind::edge_bevel:
		return qec::EdgeBevelOperationSettings {};
	case qec::EditorOperationSettingsKind::thicken:
		return qec::ThickenOperationSettings {};
	case qec::EditorOperationSettingsKind::slice_quad:
		return qec::SliceQuadOperationSettings {};
	case qec::EditorOperationSettingsKind::merge_by_distance:
		return qec::MergeByDistanceOperationSettings {};
	case qec::EditorOperationSettingsKind::bevel_vertices:
		return qec::BevelVerticesOperationSettings {};
	case qec::EditorOperationSettingsKind::duplicate_meshes:
		return qec::DuplicateMeshesOperationSettings {};
	case qec::EditorOperationSettingsKind::recalculate_face_normals:
		return qec::RecalculateFaceNormalsOperationSettings {};
	case qec::EditorOperationSettingsKind::assign_material:
		return qec::AssignMaterialOperationSettings {};
	}
	return qec::SelectionOperationSettings {};
}

} // namespace

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandSchemaOwnsSettingsAndPreviewMappings) {
	for (const qec::EditorOperationRegistryEntry& entry : qec::operation_registry().entries()) {
		const std::optional<qec::EditorOperationCommandSchema> schema =
			qec::command_schema_for_operation(entry.operation_id);
		EXPECT_TRUE(schema.has_value());
		EXPECT_TRUE(schema->operation_id == entry.operation_id);
		EXPECT_TRUE(qec::command_settings_match_operation(entry.operation_id, matching_settings(schema->settings_kind)));
		const bool accepts_wrong_bridge_settings =
			qec::command_settings_match_operation(entry.operation_id, qec::BridgeOperationSettings {}) &&
			schema->settings_kind != qec::EditorOperationSettingsKind::bridge;
		EXPECT_FALSE(accepts_wrong_bridge_settings);
		EXPECT_TRUE(qec::preview_transaction_kind_for_operation(entry.operation_id) == schema->preview_transaction_kind);
		EXPECT_TRUE(entry.operation->supports_preview() == schema->preview_transaction_kind.has_value());
		if (schema->preview_transaction_kind.has_value()) {
			EXPECT_TRUE(qec::operation_for_preview_transaction_kind(*schema->preview_transaction_kind) == entry.operation_id);
		}
	}

	EXPECT_FALSE(qec::operation_for_preview_transaction_kind(qec::PreviewTransactionKind::transform).has_value());
	EXPECT_FALSE(qec::operation_for_preview_transaction_kind(qec::PreviewTransactionKind::none).has_value());
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesOperationsThroughHistory) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	const std::size_t object_count_before = session.objects().size();

	quader_editor_core::DuplicateMeshesOperationSettings settings;
	settings.focused_grid = { 2.0, true };
	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::duplicate_meshes;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = settings;

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_FALSE(result.preview_finish.has_value());
	EXPECT_TRUE(session.objects().size() == object_count_before + 1U);
	EXPECT_TRUE(session.history_status().can_undo);

	const quader_editor_core::HistoryRestoreResult undo = session.undo();
	EXPECT_TRUE(undo.applied);
	EXPECT_TRUE(session.objects().size() == object_count_before);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesExplicitBevelDistance) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	const int object_id = session.objects().front().id;
	const quader_poly::ElementId vertex_id = session.objects().front().document.vertices.front().id;
	session.set_component_selection(
		object_id,
		qdr_test::vertex_selection({ vertex_id }, vertex_id),
		quader_editor_core::SelectionMode::vertex);

	quader_editor_core::BevelVerticesOperationSettings settings;
	settings.focused_grid = { 1.0, true };
	settings.explicit_distance = { 0.25, true };
	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::bevel_vertices;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = settings;

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_FALSE(result.preview_finish.has_value());
	EXPECT_TRUE(session.objects().front().selection.vertices.size() >= 2U);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesRecalculateFaceNormals) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::RecalculateFaceNormalsOperationSettings settings;
	settings.outside = false;
	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::recalculate_face_normals;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = settings;

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_FALSE(result.preview_finish.has_value());
	EXPECT_TRUE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, false));
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesDeleteSelectionWithLegacyNoSelectionCompatibilitySelectedObjectsAreDeletedThroughHistory) {
	{
		qec::EditorSession session = qdr_test::selected_cube_session();

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::delete_selection));

		EXPECT_TRUE(result.command.ok);
		EXPECT_TRUE(result.command.changed);
		EXPECT_FALSE(result.preview_finish.has_value());
		EXPECT_TRUE(session.objects().empty());
		EXPECT_TRUE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		EXPECT_TRUE(undo.applied);
		EXPECT_TRUE(session.objects().size() == 1U);
	}

	
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesDeleteSelectionWithLegacyNoSelectionCompatibilityMissingSelectionIsOkAndUnchanged) {
	

	{
		qec::EditorSession session = qdr_test::selected_cube_session();
		session.clear_selection();

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::delete_selection));

		EXPECT_TRUE(result.command.ok);
		EXPECT_FALSE(result.command.changed);
		EXPECT_FALSE(result.preview_finish.has_value());
		EXPECT_TRUE(session.objects().size() == 1U);
		EXPECT_FALSE(session.history_status().can_undo);
	}
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesNoArgumentSelectionOperations) {
	quader_poly::Document document = quader_poly::make_cube({});
	const quader_poly::Face removed_face = document.faces.front();
	document.faces.erase(document.faces.begin());

	quader_poly::Selection edge_selection;
	edge_selection.mode = quader_poly::SelectionMode::edge;
	for (std::size_t index = 0; index < removed_face.vertices.size(); ++index) {
		edge_selection.edges.push_back(quader_poly::make_edge(
				removed_face.vertices[index],
				removed_face.vertices[(index + 1U) % removed_face.vertices.size()]));
	}

	quader_editor_core::EditorSession session({ qdr_test::make_object(1, document) });
	session.set_component_selection(1, edge_selection, quader_editor_core::SelectionMode::edge);
	const std::size_t face_count_before = session.objects().front().document.faces.size();

	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::fill_hole;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = quader_editor_core::SelectionOperationSettings {};

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_FALSE(result.preview_finish.has_value());
	EXPECT_TRUE(session.selection_mode() == quader_editor_core::SelectionMode::face);
	EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before + 1U);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesActiveSelectionOperationsSnapVerticesToActive) {
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		qec::EditorSession session({ qdr_test::make_object(1, fixture.document) });
		session.set_component_selection(
			1,
			qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right }, fixture.bottom_left),
			qec::SelectionMode::vertex);
		const quader::QVec3 source_position_before =
			quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right)->position;

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::snap_vertices_to_active));

		EXPECT_TRUE(result.command.ok);
		EXPECT_TRUE(result.command.changed);
		const quader_poly::Vertex* active = quader_poly::find_vertex(session.objects().front().document, fixture.bottom_left);
		const quader_poly::Vertex* source = quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right);
		EXPECT_TRUE(active != nullptr);
		EXPECT_TRUE(source != nullptr);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(source->position, active->position));
		EXPECT_TRUE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		EXPECT_TRUE(undo.applied);
		source = quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right);
		EXPECT_TRUE(source != nullptr);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(source->position, source_position_before));
	}

	

	

	
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesActiveSelectionOperationsSnapEdgesToActive) {
	

	{
		qec::EditorSession session = qdr_test::selected_cube_session();
		qec::PolygonObject& object = session.objects().front();
		const quader_poly::Face first_face = object.document.faces.front();
		const quader_poly::Edge active = quader_poly::make_edge(first_face.vertices[0], first_face.vertices[1]);
		const quader_poly::Edge source_edge = quader_poly::make_edge(first_face.vertices[2], first_face.vertices[3]);
		session.set_component_selection(
			object.id,
			qdr_test::edge_selection({ active, source_edge }, active),
			qec::SelectionMode::edge);
		const quader::QVec3 source_a_before =
			quader_poly::find_vertex(session.objects().front().document, source_edge.a)->position;
		const quader::QVec3 source_b_before =
			quader_poly::find_vertex(session.objects().front().document, source_edge.b)->position;

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::snap_edges_to_active));

		EXPECT_TRUE(result.command.ok);
		EXPECT_TRUE(result.command.changed);
		const quader_poly::Vertex* active_a = quader_poly::find_vertex(session.objects().front().document, active.a);
		const quader_poly::Vertex* active_b = quader_poly::find_vertex(session.objects().front().document, active.b);
		const quader_poly::Vertex* source_a = quader_poly::find_vertex(session.objects().front().document, source_edge.a);
		const quader_poly::Vertex* source_b = quader_poly::find_vertex(session.objects().front().document, source_edge.b);
		EXPECT_TRUE(active_a != nullptr);
		EXPECT_TRUE(active_b != nullptr);
		EXPECT_TRUE(source_a != nullptr);
		EXPECT_TRUE(source_b != nullptr);
		const bool direct =
			qdr_test::vec_nearly_equal(source_a->position, active_a->position) &&
			qdr_test::vec_nearly_equal(source_b->position, active_b->position);
		const bool flipped =
			qdr_test::vec_nearly_equal(source_a->position, active_b->position) &&
			qdr_test::vec_nearly_equal(source_b->position, active_a->position);
		EXPECT_TRUE((direct || flipped));
		EXPECT_TRUE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		EXPECT_TRUE(undo.applied);
		source_a = quader_poly::find_vertex(session.objects().front().document, source_edge.a);
		source_b = quader_poly::find_vertex(session.objects().front().document, source_edge.b);
		EXPECT_TRUE(source_a != nullptr);
		EXPECT_TRUE(source_b != nullptr);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(source_a->position, source_a_before));
		EXPECT_TRUE(qdr_test::vec_nearly_equal(source_b->position, source_b_before));
	}

	

	
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesActiveSelectionOperationsMergeVerticesToActive) {
	

	

	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		qec::EditorSession session({ qdr_test::make_object(1, fixture.document) });
		session.set_component_selection(
			1,
			qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right }, fixture.bottom_left),
			qec::SelectionMode::vertex);
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::merge_vertices_to_active));

		EXPECT_TRUE(result.command.ok);
		EXPECT_TRUE(result.command.changed);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);
		EXPECT_TRUE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_left) != nullptr);
		EXPECT_TRUE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) == nullptr);
		EXPECT_TRUE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		EXPECT_TRUE(undo.applied);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before);
		EXPECT_TRUE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) != nullptr);
	}

	
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeExecutesActiveSelectionOperationsMergeVerticesToCenter) {
	

	

	

	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		qec::EditorSession session({ qdr_test::make_object(1, fixture.document) });
		session.set_component_selection(
			1,
			qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right }, fixture.bottom_left),
			qec::SelectionMode::vertex);
		const std::size_t vertex_count_before = session.objects().front().document.vertices.size();

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::merge_vertices_to_center));

		EXPECT_TRUE(result.command.ok);
		EXPECT_TRUE(result.command.changed);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);
		const quader_poly::Vertex* survivor =
			quader_poly::find_vertex(session.objects().front().document, fixture.bottom_left);
		EXPECT_TRUE(survivor != nullptr);
		EXPECT_TRUE(qdr_test::vec_nearly_equal(survivor->position, { 0.0F, -1.0F, 0.0F }));
		EXPECT_TRUE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) == nullptr);
		EXPECT_TRUE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		EXPECT_TRUE(undo.applied);
		EXPECT_TRUE(session.objects().front().document.vertices.size() == vertex_count_before);
		EXPECT_TRUE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) != nullptr);
	}
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeRejectsActiveSelectionOperationPreviews) {
	qec::EditorSession session = qdr_test::selected_cube_session();

	qec::EditorOperationCommand command = selection_execute_command(qec::EditorOperationId::snap_vertices_to_active);
	command.phase = qec::EditorOperationCommandPhase::begin_preview;
	const qec::EditorOperationCommandResult result = qec::execute_command(session, command);

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message.find("preview") != std::string::npos);
	EXPECT_FALSE(result.preview_finish.has_value());
	EXPECT_FALSE(session.preview_transaction_status().active);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeRejectsMismatchedActiveSelectionOperationSettings) {
	qec::EditorSession session = qdr_test::selected_cube_session();

	qec::EditorOperationCommand command;
	command.operation_id = qec::EditorOperationId::merge_vertices_to_active;
	command.phase = qec::EditorOperationCommandPhase::execute;
	command.settings = qec::BridgeOperationSettings {};
	const qec::EditorOperationCommandResult result = qec::execute_command(session, command);

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message.find("settings") != std::string::npos);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeRoutesPreviewPhases) {
	quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
	quader_editor_core::EditorSession session({ object });
	session.set_component_selection(
		1,
		qdr_test::face_selection({ session.objects().front().document.faces.front().id }),
		quader_editor_core::SelectionMode::face);
	const std::size_t face_count_before = session.objects().front().document.faces.size();

	quader_editor_core::ThickenOperationSettings settings;
	settings.thickness = 0.25;
	settings.from_center = true;
	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::thicken;
	command.settings = settings;

	command.phase = quader_editor_core::EditorOperationCommandPhase::begin_preview;
	const quader_editor_core::EditorOperationCommandResult begin =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(begin.command.ok);
	EXPECT_TRUE(begin.command.changed);
	EXPECT_FALSE(begin.preview_finish.has_value());
	EXPECT_TRUE(session.preview_transaction_status().active);
	EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
	EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);

	settings.thickness = 0.5;
	command.settings = settings;
	command.phase = quader_editor_core::EditorOperationCommandPhase::update_preview;
	const quader_editor_core::EditorOperationCommandResult update =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(update.command.ok);
	EXPECT_TRUE(update.command.changed);
	EXPECT_FALSE(update.preview_finish.has_value());

	command.phase = quader_editor_core::EditorOperationCommandPhase::cancel_preview;
	const quader_editor_core::EditorOperationCommandResult cancel =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(cancel.command.ok);
	EXPECT_TRUE(cancel.command.changed);
	EXPECT_TRUE(cancel.preview_finish.has_value());
	EXPECT_TRUE(cancel.preview_finish->restored);
	EXPECT_FALSE(session.preview_transaction_status().active);
	EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
	EXPECT_FALSE(session.history_status().can_undo);

	settings.thickness = 0.25;
	command.settings = settings;
	command.phase = quader_editor_core::EditorOperationCommandPhase::begin_preview;
	EXPECT_TRUE(quader_editor_core::execute_command(session, command).command.changed);

	command.phase = quader_editor_core::EditorOperationCommandPhase::commit_preview;
	const quader_editor_core::EditorOperationCommandResult commit =
		quader_editor_core::execute_command(session, command);

	EXPECT_TRUE(commit.command.ok);
	EXPECT_TRUE(commit.command.changed);
	EXPECT_TRUE(commit.preview_finish.has_value());
	EXPECT_TRUE(commit.preview_finish->before.changed);
	EXPECT_FALSE(commit.preview_finish->restored);
	EXPECT_TRUE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeRejectsPreviewPhasesForNonPreviewOperations) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::duplicate_meshes;
	command.phase = quader_editor_core::EditorOperationCommandPhase::begin_preview;
	command.settings = quader_editor_core::DuplicateMeshesOperationSettings {};

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_FALSE(result.preview_finish.has_value());
	EXPECT_FALSE(session.preview_transaction_status().active);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandFacadeRejectsMismatchedSettings) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	const std::size_t object_count_before = session.objects().size();

	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::duplicate_meshes;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = quader_editor_core::BridgeOperationSettings {};

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	EXPECT_FALSE(result.command.ok);
	EXPECT_FALSE(result.command.changed);
	EXPECT_TRUE(result.command.message.find("settings") != std::string::npos);
	EXPECT_TRUE(session.objects().size() == object_count_before);
	EXPECT_FALSE(session.history_status().can_undo);
}

TEST(QdrModelingRuntimeOperationCommandTests, EditorOperationCommandDispatchCompatibilityForwardsToExecuteCommand) {
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::duplicate_meshes;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = quader_editor_core::DuplicateMeshesOperationSettings {};

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::dispatch_editor_operation_command(session, command);

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
}

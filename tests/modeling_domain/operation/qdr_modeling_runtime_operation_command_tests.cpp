////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/modeling/operations/operation_command.h>

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

} // namespace

TEST_CASE("editor operation command facade executes operations through history", "[operation][command]")
{
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

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE_FALSE(result.preview_finish.has_value());
	REQUIRE(session.objects().size() == object_count_before + 1U);
	REQUIRE(session.history_status().can_undo);

	const quader_editor_core::HistoryRestoreResult undo = session.undo();
	REQUIRE(undo.applied);
	REQUIRE(session.objects().size() == object_count_before);
}

TEST_CASE("editor operation command facade executes explicit bevel distance", "[operation][command]")
{
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

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE_FALSE(result.preview_finish.has_value());
	REQUIRE(session.objects().front().selection.vertices.size() >= 2U);
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor operation command facade executes recalculate face normals", "[operation][command]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::RecalculateFaceNormalsOperationSettings settings;
	settings.outside = false;
	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::recalculate_face_normals;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = settings;

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE_FALSE(result.preview_finish.has_value());
	REQUIRE(qdr_test::all_faces_point_relative_to_center(session.objects().front().document, false));
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor operation command facade executes delete selection with legacy no-selection compatibility", "[operation][command]")
{
	SECTION("selected objects are deleted through history")
	{
		qec::EditorSession session = qdr_test::selected_cube_session();

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::delete_selection));

		REQUIRE(result.command.ok);
		REQUIRE(result.command.changed);
		REQUIRE_FALSE(result.preview_finish.has_value());
		REQUIRE(session.objects().empty());
		REQUIRE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		REQUIRE(undo.applied);
		REQUIRE(session.objects().size() == 1U);
	}

	SECTION("missing selection is ok and unchanged")
	{
		qec::EditorSession session = qdr_test::selected_cube_session();
		session.clear_selection();

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::delete_selection));

		REQUIRE(result.command.ok);
		REQUIRE_FALSE(result.command.changed);
		REQUIRE_FALSE(result.preview_finish.has_value());
		REQUIRE(session.objects().size() == 1U);
		REQUIRE_FALSE(session.history_status().can_undo);
	}
}

TEST_CASE("editor operation command facade executes no-argument selection operations", "[operation][command]")
{
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

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE_FALSE(result.preview_finish.has_value());
	REQUIRE(session.selection_mode() == quader_editor_core::SelectionMode::face);
	REQUIRE(session.objects().front().document.faces.size() == face_count_before + 1U);
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor operation command facade executes active selection operations", "[operation][command]")
{
	SECTION("snap vertices to active")
	{
		qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
		qec::EditorSession session({ qdr_test::make_object(1, fixture.document) });
		session.set_component_selection(
			1,
			qdr_test::vertex_selection({ fixture.bottom_left, fixture.bottom_right }, fixture.bottom_left),
			qec::SelectionMode::vertex);
		const quader_poly::Vec3 source_position_before =
			quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right)->position;

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::snap_vertices_to_active));

		REQUIRE(result.command.ok);
		REQUIRE(result.command.changed);
		const quader_poly::Vertex* active = quader_poly::find_vertex(session.objects().front().document, fixture.bottom_left);
		const quader_poly::Vertex* source = quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right);
		REQUIRE(active != nullptr);
		REQUIRE(source != nullptr);
		REQUIRE(qdr_test::vec_nearly_equal(source->position, active->position));
		REQUIRE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		REQUIRE(undo.applied);
		source = quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right);
		REQUIRE(source != nullptr);
		REQUIRE(qdr_test::vec_nearly_equal(source->position, source_position_before));
	}

	SECTION("snap edges to active")
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
		const quader_poly::Vec3 source_a_before =
			quader_poly::find_vertex(session.objects().front().document, source_edge.a)->position;
		const quader_poly::Vec3 source_b_before =
			quader_poly::find_vertex(session.objects().front().document, source_edge.b)->position;

		const qec::EditorOperationCommandResult result =
			qec::execute_command(session, selection_execute_command(qec::EditorOperationId::snap_edges_to_active));

		REQUIRE(result.command.ok);
		REQUIRE(result.command.changed);
		const quader_poly::Vertex* active_a = quader_poly::find_vertex(session.objects().front().document, active.a);
		const quader_poly::Vertex* active_b = quader_poly::find_vertex(session.objects().front().document, active.b);
		const quader_poly::Vertex* source_a = quader_poly::find_vertex(session.objects().front().document, source_edge.a);
		const quader_poly::Vertex* source_b = quader_poly::find_vertex(session.objects().front().document, source_edge.b);
		REQUIRE(active_a != nullptr);
		REQUIRE(active_b != nullptr);
		REQUIRE(source_a != nullptr);
		REQUIRE(source_b != nullptr);
		const bool direct =
			qdr_test::vec_nearly_equal(source_a->position, active_a->position) &&
			qdr_test::vec_nearly_equal(source_b->position, active_b->position);
		const bool flipped =
			qdr_test::vec_nearly_equal(source_a->position, active_b->position) &&
			qdr_test::vec_nearly_equal(source_b->position, active_a->position);
		REQUIRE((direct || flipped));
		REQUIRE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		REQUIRE(undo.applied);
		source_a = quader_poly::find_vertex(session.objects().front().document, source_edge.a);
		source_b = quader_poly::find_vertex(session.objects().front().document, source_edge.b);
		REQUIRE(source_a != nullptr);
		REQUIRE(source_b != nullptr);
		REQUIRE(qdr_test::vec_nearly_equal(source_a->position, source_a_before));
		REQUIRE(qdr_test::vec_nearly_equal(source_b->position, source_b_before));
	}

	SECTION("merge vertices to active")
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

		REQUIRE(result.command.ok);
		REQUIRE(result.command.changed);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);
		REQUIRE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_left) != nullptr);
		REQUIRE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) == nullptr);
		REQUIRE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		REQUIRE(undo.applied);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before);
		REQUIRE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) != nullptr);
	}

	SECTION("merge vertices to center")
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

		REQUIRE(result.command.ok);
		REQUIRE(result.command.changed);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before - 1U);
		const quader_poly::Vertex* survivor =
			quader_poly::find_vertex(session.objects().front().document, fixture.bottom_left);
		REQUIRE(survivor != nullptr);
		REQUIRE(qdr_test::vec_nearly_equal(survivor->position, { 0.0F, -1.0F, 0.0F }));
		REQUIRE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) == nullptr);
		REQUIRE(session.history_status().can_undo);

		const qec::HistoryRestoreResult undo = session.undo();
		REQUIRE(undo.applied);
		REQUIRE(session.objects().front().document.vertices.size() == vertex_count_before);
		REQUIRE(quader_poly::find_vertex(session.objects().front().document, fixture.bottom_right) != nullptr);
	}
}

TEST_CASE("editor operation command facade rejects active selection operation previews", "[operation][command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();

	qec::EditorOperationCommand command = selection_execute_command(qec::EditorOperationId::snap_vertices_to_active);
	command.phase = qec::EditorOperationCommandPhase::begin_preview;
	const qec::EditorOperationCommandResult result = qec::execute_command(session, command);

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message.find("preview") != std::string::npos);
	REQUIRE_FALSE(result.preview_finish.has_value());
	REQUIRE_FALSE(session.preview_transaction_status().active);
}

TEST_CASE("editor operation command facade rejects mismatched active selection operation settings", "[operation][command]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();

	qec::EditorOperationCommand command;
	command.operation_id = qec::EditorOperationId::merge_vertices_to_active;
	command.phase = qec::EditorOperationCommandPhase::execute;
	command.settings = qec::BridgeOperationSettings {};
	const qec::EditorOperationCommandResult result = qec::execute_command(session, command);

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message.find("settings") != std::string::npos);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor operation command facade routes preview phases", "[operation][command]")
{
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

	REQUIRE(begin.command.ok);
	REQUIRE(begin.command.changed);
	REQUIRE_FALSE(begin.preview_finish.has_value());
	REQUIRE(session.preview_transaction_status().active);
	REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::thicken);
	REQUIRE(session.objects().front().document.faces.size() > face_count_before);

	settings.thickness = 0.5;
	command.settings = settings;
	command.phase = quader_editor_core::EditorOperationCommandPhase::update_preview;
	const quader_editor_core::EditorOperationCommandResult update =
		quader_editor_core::execute_command(session, command);

	REQUIRE(update.command.ok);
	REQUIRE(update.command.changed);
	REQUIRE_FALSE(update.preview_finish.has_value());

	command.phase = quader_editor_core::EditorOperationCommandPhase::cancel_preview;
	const quader_editor_core::EditorOperationCommandResult cancel =
		quader_editor_core::execute_command(session, command);

	REQUIRE(cancel.command.ok);
	REQUIRE(cancel.command.changed);
	REQUIRE(cancel.preview_finish.has_value());
	REQUIRE(cancel.preview_finish->restored);
	REQUIRE_FALSE(session.preview_transaction_status().active);
	REQUIRE(session.objects().front().document.faces.size() == face_count_before);
	REQUIRE_FALSE(session.history_status().can_undo);

	settings.thickness = 0.25;
	command.settings = settings;
	command.phase = quader_editor_core::EditorOperationCommandPhase::begin_preview;
	REQUIRE(quader_editor_core::execute_command(session, command).command.changed);

	command.phase = quader_editor_core::EditorOperationCommandPhase::commit_preview;
	const quader_editor_core::EditorOperationCommandResult commit =
		quader_editor_core::execute_command(session, command);

	REQUIRE(commit.command.ok);
	REQUIRE(commit.command.changed);
	REQUIRE(commit.preview_finish.has_value());
	REQUIRE(commit.preview_finish->before.changed);
	REQUIRE_FALSE(commit.preview_finish->restored);
	REQUIRE(session.history_status().can_undo);
}

TEST_CASE("editor operation command facade rejects preview phases for non-preview operations", "[operation][command]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::duplicate_meshes;
	command.phase = quader_editor_core::EditorOperationCommandPhase::begin_preview;
	command.settings = quader_editor_core::DuplicateMeshesOperationSettings {};

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE_FALSE(result.preview_finish.has_value());
	REQUIRE_FALSE(session.preview_transaction_status().active);
}

TEST_CASE("editor operation command facade rejects mismatched settings", "[operation][command]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
	const std::size_t object_count_before = session.objects().size();

	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::duplicate_meshes;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = quader_editor_core::BridgeOperationSettings {};

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::execute_command(session, command);

	REQUIRE_FALSE(result.command.ok);
	REQUIRE_FALSE(result.command.changed);
	REQUIRE(result.command.message.find("settings") != std::string::npos);
	REQUIRE(session.objects().size() == object_count_before);
	REQUIRE_FALSE(session.history_status().can_undo);
}

TEST_CASE("editor operation command dispatch compatibility forwards to execute_command", "[operation][command]")
{
	quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

	quader_editor_core::EditorOperationCommand command;
	command.operation_id = quader_editor_core::EditorOperationId::duplicate_meshes;
	command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
	command.settings = quader_editor_core::DuplicateMeshesOperationSettings {};

	const quader_editor_core::EditorOperationCommandResult result =
		quader_editor_core::dispatch_editor_operation_command(session, command);

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE(session.objects().size() == 2U);
}

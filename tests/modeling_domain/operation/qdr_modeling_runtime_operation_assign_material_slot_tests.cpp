////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/modeling/operations/operation_command.h>

#include <algorithm>

namespace qec = quader_editor_core;
namespace qp = quader_poly;

namespace {

void set_all_face_slots(qp::Document& document, std::uint32_t slot)
{
	for (qp::Face& face : document.faces) {
		face.material_slot = slot;
	}
}

std::uint32_t material_slot_for_face(const qec::EditorSession& session, int object_id, qp::ElementId face_id)
{
	const auto object = std::ranges::find_if(session.objects(), [object_id](const qec::PolygonObject& candidate) {
		return candidate.id == object_id;
	});
	REQUIRE(object != session.objects().end());
	const qp::Face* face = qp::find_face(object->document, face_id);
	REQUIRE(face != nullptr);
	return face->material_slot;
}

bool all_faces_use_slot(const qec::PolygonObject& object, std::uint32_t slot)
{
	return std::ranges::all_of(object.document.faces, [slot](const qp::Face& face) {
		return face.material_slot == slot;
	});
}

} // namespace

TEST_CASE("editor session assigns material slots to selected faces in face mode", "[operation][material_slot]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::PolygonObject& object = session.objects().front();
	REQUIRE(object.document.faces.size() >= 2U);
	const int object_id = object.id;
	const qp::ElementId selected_face_id = object.document.faces[0].id;
	const qp::ElementId untouched_face_id = object.document.faces[1].id;
	set_all_face_slots(object.document, 6U);
	session.set_component_selection(
		object_id,
		qdr_test::face_selection({ selected_face_id }, selected_face_id),
		qec::SelectionMode::face);

	const qec::CommandResult result = session.assign_material_to_selection(0U);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(material_slot_for_face(session, object_id, selected_face_id) == 0U);
	REQUIRE(material_slot_for_face(session, object_id, untouched_face_id) == 6U);
}

TEST_CASE("editor session assigns material slots to all faces of selected objects in object mode", "[operation][material_slot]")
{
	qp::Document first = qp::make_cube({});
	qp::Document second = qp::make_cube({});
	set_all_face_slots(first, 1U);
	set_all_face_slots(second, 2U);
	qec::EditorSession session({
		qdr_test::make_object(10, std::move(first), false),
		qdr_test::make_object(20, std::move(second), false),
	});
	session.set_selected_object_ids({ 10 }, 10);

	const qec::CommandResult result = session.assign_material_to_selection(4U);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(all_faces_use_slot(session.objects()[0], 4U));
	REQUIRE(all_faces_use_slot(session.objects()[1], 2U));
}

TEST_CASE("editor session assigns material slots to one explicit face target", "[operation][material_slot]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	const int object_id = session.objects().front().id;
	REQUIRE(session.objects().front().document.faces.size() >= 2U);
	const qp::ElementId target_face_id = session.objects().front().document.faces[1].id;
	const qp::ElementId untouched_face_id = session.objects().front().document.faces[0].id;

	const qec::CommandResult result = session.assign_material_to_face(object_id, target_face_id, 9U);

	REQUIRE(result.ok);
	REQUIRE(result.changed);
	REQUIRE(material_slot_for_face(session, object_id, target_face_id) == 9U);
	REQUIRE(material_slot_for_face(session, object_id, untouched_face_id) == 0U);
}

TEST_CASE("editor session material slot assignment participates in undo and redo", "[operation][material_slot]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	const int object_id = session.objects().front().id;
	const qp::ElementId face_id = session.objects().front().document.faces.front().id;
	session.set_component_selection(
		object_id,
		qdr_test::face_selection({ face_id }, face_id),
		qec::SelectionMode::face);

	const qec::CommandResult command =
		session.apply_history_command([](qec::EditorSession& editor) {
			return editor.assign_material_to_selection(12U);
		});

	REQUIRE(command.ok);
	REQUIRE(command.changed);
	REQUIRE(material_slot_for_face(session, object_id, face_id) == 12U);
	REQUIRE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	REQUIRE(undo.applied);
	REQUIRE(material_slot_for_face(session, object_id, face_id) == 0U);
	REQUIRE(session.history_status().can_redo);

	const qec::HistoryRestoreResult redo = session.redo();
	REQUIRE(redo.applied);
	REQUIRE(material_slot_for_face(session, object_id, face_id) == 12U);
}

TEST_CASE("editor operation command assigns explicit material target through history", "[operation][material_slot]")
{
	qec::EditorSession session = qdr_test::selected_cube_session();
	const int object_id = session.objects().front().id;
	const qp::ElementId face_id = session.objects().front().document.faces.front().id;

	qec::AssignMaterialOperationSettings settings;
	settings.material_slot = 15U;
	settings.explicit_target = true;
	settings.object_id = object_id;
	settings.face_id = face_id;
	qec::EditorOperationCommand command;
	command.operation_id = qec::EditorOperationId::assign_material;
	command.phase = qec::EditorOperationCommandPhase::execute;
	command.settings = settings;

	const qec::EditorOperationCommandResult result = qec::execute_command(session, command);

	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);
	REQUIRE_FALSE(result.preview_finish.has_value());
	REQUIRE(material_slot_for_face(session, object_id, face_id) == 15U);
	REQUIRE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	REQUIRE(undo.applied);
	REQUIRE(material_slot_for_face(session, object_id, face_id) == 0U);
}

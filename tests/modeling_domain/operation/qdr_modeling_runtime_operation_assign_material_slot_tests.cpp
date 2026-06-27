////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_command.hpp>

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
	EXPECT_TRUE(object != session.objects().end());
	const qp::Face* face = qp::find_face(object->document, face_id);
	EXPECT_TRUE(face != nullptr);
	return face->material_slot;
}

bool all_faces_use_slot(const qec::PolygonObject& object, std::uint32_t slot)
{
	return std::ranges::all_of(object.document.faces, [slot](const qp::Face& face) {
		return face.material_slot == slot;
	});
}

} // namespace

TEST(QdrModelingRuntimeOperationAssignMaterialSlotTests, EditorSessionAssignsMaterialSlotsToSelectedFacesInFaceMode) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	qec::PolygonObject& object = session.objects().front();
	EXPECT_TRUE(object.document.faces.size() >= 2U);
	const int object_id = object.id;
	const qp::ElementId selected_face_id = object.document.faces[0].id;
	const qp::ElementId untouched_face_id = object.document.faces[1].id;
	set_all_face_slots(object.document, 6U);
	session.set_component_selection(
		object_id,
		qdr_test::face_selection({ selected_face_id }, selected_face_id),
		qec::SelectionMode::face);

	const qec::CommandResult result = session.assign_material_to_selection(0U);

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(material_slot_for_face(session, object_id, selected_face_id) == 0U);
	EXPECT_TRUE(material_slot_for_face(session, object_id, untouched_face_id) == 6U);
}

TEST(QdrModelingRuntimeOperationAssignMaterialSlotTests, EditorSessionAssignsMaterialSlotsToAllFacesOfSelectedObjectsInObjectMode) {
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

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(all_faces_use_slot(session.objects()[0], 4U));
	EXPECT_TRUE(all_faces_use_slot(session.objects()[1], 2U));
}

TEST(QdrModelingRuntimeOperationAssignMaterialSlotTests, EditorSessionAssignsMaterialSlotsToOneExplicitFaceTarget) {
	qec::EditorSession session = qdr_test::selected_cube_session();
	const int object_id = session.objects().front().id;
	EXPECT_TRUE(session.objects().front().document.faces.size() >= 2U);
	const qp::ElementId target_face_id = session.objects().front().document.faces[1].id;
	const qp::ElementId untouched_face_id = session.objects().front().document.faces[0].id;

	const qec::CommandResult result = session.assign_material_to_face(object_id, target_face_id, 9U);

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(material_slot_for_face(session, object_id, target_face_id) == 9U);
	EXPECT_TRUE(material_slot_for_face(session, object_id, untouched_face_id) == 0U);
}

TEST(QdrModelingRuntimeOperationAssignMaterialSlotTests, EditorSessionMaterialSlotAssignmentParticipatesInUndoAndRedo) {
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

	EXPECT_TRUE(command.ok);
	EXPECT_TRUE(command.changed);
	EXPECT_TRUE(material_slot_for_face(session, object_id, face_id) == 12U);
	EXPECT_TRUE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	EXPECT_TRUE(undo.applied);
	EXPECT_TRUE(material_slot_for_face(session, object_id, face_id) == 0U);
	EXPECT_TRUE(session.history_status().can_redo);

	const qec::HistoryRestoreResult redo = session.redo();
	EXPECT_TRUE(redo.applied);
	EXPECT_TRUE(material_slot_for_face(session, object_id, face_id) == 12U);
}

TEST(QdrModelingRuntimeOperationAssignMaterialSlotTests, EditorOperationCommandAssignsExplicitMaterialTargetThroughHistory) {
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

	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);
	EXPECT_FALSE(result.preview_finish.has_value());
	EXPECT_TRUE(material_slot_for_face(session, object_id, face_id) == 15U);
	EXPECT_TRUE(session.history_status().can_undo);

	const qec::HistoryRestoreResult undo = session.undo();
	EXPECT_TRUE(undo.applied);
	EXPECT_TRUE(material_slot_for_face(session, object_id, face_id) == 0U);
}

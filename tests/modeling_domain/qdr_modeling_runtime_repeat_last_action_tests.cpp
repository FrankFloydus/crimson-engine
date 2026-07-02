////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/repeat_last_action.hpp>

#include <array>
#include <utility>
#include <variant>

namespace qec = quader_editor_core;

namespace {

qec::EditorSession selected_box_session(
		quader::QVec3 min_bounds = { -1.0F, -1.0F, -1.0F },
		quader::QVec3 max_bounds = { 1.0F, 1.0F, 1.0F })
{
	qec::EditorSession session;
	const qec::CommandResult create = session.create_box_from_bounds(min_bounds, max_bounds);
	EXPECT_TRUE(create.ok);
	EXPECT_TRUE(create.changed);
	EXPECT_TRUE(session.objects().size() == 1U);
	return session;
}

qec::EditorSession selected_quad_face_session()
{
	quader_poly::Document document;
	const quader_poly::ElementId bottom_left = quader_poly::add_vertex(document, { -1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId bottom_right = quader_poly::add_vertex(document, { 1.0F, -1.0F, 0.0F });
	const quader_poly::ElementId top_right = quader_poly::add_vertex(document, { 1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId top_left = quader_poly::add_vertex(document, { -1.0F, 1.0F, 0.0F });
	const quader_poly::ElementId face = quader_poly::add_face(
		document,
		std::array<quader_poly::ElementId, 4> { bottom_left, bottom_right, top_right, top_left });
        EXPECT_TRUE(face != quader_poly::kInvalidElementId);

        qec::PolygonObject object;
	object.id = 1;
	object.selected = true;
	object.document = std::move(document);
	qec::EditorSession session({ object });
	quader_poly::Selection selection;
        selection.mode = quader_poly::SelectionMode::Face;
        selection.faces = { face };
	selection.has_active = true;
        selection.active_kind = quader_poly::ElementKind::Face;
        selection.active_face = face;
        session.set_component_selection(object.id, selection,
                                        qec::SelectionMode::Face);
        return session;
}

qec::EditorOperationCommand selection_command(qec::EditorOperationId operation_id)
{
	qec::EditorOperationCommand command;
	command.operation_id = operation_id;
        command.phase = qec::EditorOperationCommandPhase::Execute;
        command.settings = qec::SelectionOperationSettings {};
	return command;
}

qec::EditorOperationCommand duplicate_command(quader::QVec3 offset)
{
	qec::DuplicateMeshesOperationSettings settings;
	settings.explicit_offset = { offset, true };
	qec::EditorOperationCommand command;
        command.operation_id = qec::EditorOperationId::DuplicateMeshes;
        command.phase = qec::EditorOperationCommandPhase::Execute;
        command.settings = settings;
	return command;
}

qec::EditorOperationCommand thicken_command(double thickness, bool from_center = true)
{
	qec::ThickenOperationSettings settings;
	settings.thickness = thickness;
	settings.from_center = from_center;
	qec::EditorOperationCommand command;
        command.operation_id = qec::EditorOperationId::Thicken;
        command.phase = qec::EditorOperationCommandPhase::Execute;
        command.settings = settings;
	return command;
}

void select_first_face(qec::EditorSession& session)
{
	const qec::PolygonObject& object = session.objects().front();
	EXPECT_FALSE(object.document.faces.empty());
	const quader_poly::ElementId selected_face = object.document.faces.front().id;
	quader_poly::Selection selection;
        selection.mode = quader_poly::SelectionMode::Face;
        selection.faces = { selected_face };
	selection.has_active = true;
        selection.active_kind = quader_poly::ElementKind::Face;
        selection.active_face = selected_face;
        session.set_component_selection(object.id, selection,
                                        qec::SelectionMode::Face);
}

qec::TransformDragState move_drag(quader::QVec3 delta,
                                  qec::TransformDragOperation operation =
                                      qec::TransformDragOperation::Transform) {
  qec::TransformDragState drag;
  drag.active = true;
  drag.tool = qec::ViewportInteractionTool::Move;
  drag.operation = operation;
  drag.axis = qec::TransformAxis::X;
  drag.move.preview_delta = delta;
  return drag;
}

qec::TransformDragState rotate_drag(double angle_degrees)
{
	qec::TransformDragState drag;
	drag.active = true;
        drag.tool = qec::ViewportInteractionTool::Rotate;
        drag.operation = qec::TransformDragOperation::Transform;
        drag.axis = qec::TransformAxis::Z;
        drag.rotate.preview_angle_degrees = angle_degrees;
	return drag;
}

qec::TransformDragState scale_drag(quader::QVec3 scale,
                                   qec::TransformDragOperation operation =
                                       qec::TransformDragOperation::Transform) {
  qec::TransformDragState drag;
  drag.active = true;
  drag.tool = qec::ViewportInteractionTool::Scale;
  drag.operation = operation;
  drag.axis = qec::TransformAxis::All;
  drag.scale.preview_scale = scale;
  return drag;
}

const qec::PolygonObject& only_object(const qec::EditorSession& session)
{
	EXPECT_TRUE(session.objects().size() == 1U);
	return session.objects().front();
}

} // namespace

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionReportsNoPriorPortableAction) {
	qec::EditorSession session = selected_box_session();
	qec::RepeatLastActionState state;

	const qec::CommandResult result = qec::execute_repeat_last_action(session, state);

	EXPECT_FALSE(result.ok);
	EXPECT_FALSE(result.changed);
	EXPECT_TRUE(result.message == "No repeatable action is available.");
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionIgnoresNonRepeatableDeleteCommands) {
	qec::EditorSession session = selected_box_session();
	qec::RepeatLastActionState state;
        const qec::EditorOperationCommand command =
            selection_command(qec::EditorOperationId::DeleteSelection);
        const qec::EditorOperationCommandResult result = qec::execute_command(session, command);
	EXPECT_TRUE(result.command.ok);
	EXPECT_TRUE(result.command.changed);

	qec::record_repeatable_operation(state, command, result.command);
	const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

        EXPECT_FALSE(qec::operation_is_repeatable(
            qec::EditorOperationId::DeleteSelection));
        EXPECT_FALSE(repeat.ok);
	EXPECT_FALSE(repeat.changed);
	EXPECT_TRUE(repeat.message == "No repeatable action is available.");
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionReplaysCapturedOperationCommands) {
	qec::EditorSession session = selected_box_session();
	qec::RepeatLastActionState state;
        const qec::EditorOperationCommand command =
            selection_command(qec::EditorOperationId::FlipFaceNormals);

        const qec::EditorOperationCommandResult first = qec::execute_command(session, command);
	EXPECT_TRUE(first.command.ok);
	EXPECT_TRUE(first.command.changed);
	qec::record_repeatable_operation(state, command, first.command);

	const qec::EditorOperationCommandResult second = qec::execute_command(session, command);
	EXPECT_TRUE(second.command.ok);
	EXPECT_TRUE(second.command.changed);
	const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

	EXPECT_TRUE(repeat.ok);
	EXPECT_TRUE(repeat.changed);
	EXPECT_TRUE(session.history_status().undo_count == 3U);
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionKeepsThePriorActionAfterFailedOrUnchangedOperationResultsFailed) {
	{
		qec::EditorSession session = selected_box_session();
		qec::RepeatLastActionState state;
		const qec::EditorOperationCommand previous = duplicate_command({ 2.0F, 0.0F, 0.0F });
		const qec::EditorOperationCommandResult first = qec::execute_command(session, previous);
		EXPECT_TRUE(first.command.ok);
		EXPECT_TRUE(first.command.changed);
		qec::record_repeatable_operation(state, previous, first.command);

                qec::EditorOperationCommand failed =
                    selection_command(qec::EditorOperationId::FlipFaceNormals);
                failed.settings = qec::BridgeOperationSettings {};
		const qec::EditorOperationCommandResult failed_result = qec::execute_command(session, failed);
		EXPECT_FALSE(failed_result.command.ok);
		qec::record_repeatable_operation(state, failed, failed_result.command);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		EXPECT_TRUE(repeat.ok);
		EXPECT_TRUE(repeat.changed);
		EXPECT_TRUE(session.objects().size() == 3U);
	}

	
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionKeepsThePriorActionAfterFailedOrUnchangedOperationResultsUnchanged) {
	

	{
		qec::EditorSession session = selected_box_session();
		qec::RepeatLastActionState state;
		const qec::EditorOperationCommand previous = duplicate_command({ 2.0F, 0.0F, 0.0F });
		const qec::EditorOperationCommandResult first = qec::execute_command(session, previous);
		EXPECT_TRUE(first.command.ok);
		EXPECT_TRUE(first.command.changed);
		qec::record_repeatable_operation(state, previous, first.command);

                const qec::EditorOperationCommand ignored =
                    selection_command(qec::EditorOperationId::FlipFaceNormals);
                qec::record_repeatable_operation(state, ignored, { true, false, {} });

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		EXPECT_TRUE(repeat.ok);
		EXPECT_TRUE(repeat.changed);
		EXPECT_TRUE(session.objects().size() == 3U);
	}
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionPromotesPreviewSettingsOnlyAfterChangedCommitsChangedCommitCapturesThePendingExecuteSettings) {
	{
		qec::EditorSession session = selected_quad_face_session();
		const std::size_t original_face_count = session.objects().front().document.faces.size();
		qec::RepeatLastActionState state;

		qec::EditorOperationCommand command = thicken_command(0.25);
                command.phase = qec::EditorOperationCommandPhase::BeginPreview;
                const qec::EditorOperationCommandResult begin = qec::execute_command(session, command);
		EXPECT_TRUE(begin.command.ok);
		EXPECT_TRUE(begin.command.changed);
		qec::record_repeatable_operation(state, command, begin.command);

		command = thicken_command(0.5);
                command.phase = qec::EditorOperationCommandPhase::UpdatePreview;
                const qec::EditorOperationCommandResult update = qec::execute_command(session, command);
		EXPECT_TRUE(update.command.ok);
		EXPECT_TRUE(update.command.changed);
		qec::record_repeatable_operation(state, command, update.command);

                command.phase = qec::EditorOperationCommandPhase::CommitPreview;
                const qec::EditorOperationCommandResult commit = qec::execute_command(session, command);
		EXPECT_TRUE(commit.command.ok);
		EXPECT_TRUE(commit.command.changed);
		qec::record_repeatable_operation(state, command, commit.command);

		const std::size_t committed_face_count = session.objects().front().document.faces.size();
		EXPECT_TRUE(committed_face_count > original_face_count);

                EXPECT_TRUE(state.last_action_kind ==
                            qec::RepeatableActionKind::Operation);
                EXPECT_TRUE(state.last_operation.has_value());
		EXPECT_FALSE(state.pending_preview_operation.has_value());
                EXPECT_TRUE(state.last_operation->operation_id ==
                            qec::EditorOperationId::Thicken);
                EXPECT_TRUE(state.last_operation->phase ==
                            qec::EditorOperationCommandPhase::Execute);
                const qec::ThickenOperationSettings stored =
			std::get<qec::ThickenOperationSettings>(state.last_operation->settings);
		EXPECT_NEAR(stored.thickness, 0.5, 0.0001);
	}

	
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionPromotesPreviewSettingsOnlyAfterChangedCommitsCancelClearsPendingSettings) {
	

	{
		qec::EditorSession session = selected_quad_face_session();
		qec::RepeatLastActionState state;

		qec::EditorOperationCommand command = thicken_command(0.25);
                command.phase = qec::EditorOperationCommandPhase::BeginPreview;
                const qec::EditorOperationCommandResult begin = qec::execute_command(session, command);
		EXPECT_TRUE(begin.command.ok);
		qec::record_repeatable_operation(state, command, begin.command);

                command.phase = qec::EditorOperationCommandPhase::CancelPreview;
                const qec::EditorOperationCommandResult cancel = qec::execute_command(session, command);
		EXPECT_TRUE(cancel.command.ok);
		qec::record_repeatable_operation(state, command, cancel.command);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		EXPECT_FALSE(repeat.ok);
		EXPECT_FALSE(repeat.changed);
		EXPECT_TRUE(repeat.message == "No repeatable action is available.");
	}
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionReplaysDuplicateMoveTransforms) {
	qec::EditorSession session = selected_box_session();
	const qec::Bounds3 original_bounds = session.document_bounds();
	qec::RepeatLastActionState state;
	qec::record_repeatable_transform(state, session, move_drag({ 2.0F, 0.0F, 0.0F }), true);

	const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

	EXPECT_TRUE(repeat.ok);
	EXPECT_TRUE(repeat.changed);
	EXPECT_TRUE(session.objects().size() == 2U);
	const qec::Bounds3 moved_bounds = session.document_bounds();
	EXPECT_NEAR(moved_bounds.max.x, original_bounds.max.x + 2.0F, 0.0001);
	EXPECT_TRUE(session.history_status().undo_count == 1U);
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionReplaysRotateAndScaleTransformsRotate) {
	{
		qec::EditorSession session = selected_box_session({ -1.0F, -2.0F, -0.5F }, { 1.0F, 2.0F, 0.5F });
		qec::RepeatLastActionState state;
		qec::record_repeatable_transform(state, session, rotate_drag(90.0), false);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		EXPECT_TRUE(repeat.ok);
		EXPECT_TRUE(repeat.changed);
		const qec::Bounds3 bounds = session.document_bounds();
		EXPECT_NEAR(bounds.min.x, -2.0F, 0.0001);
		EXPECT_NEAR(bounds.max.x, 2.0F, 0.0001);
		EXPECT_NEAR(bounds.min.y, -1.0F, 0.0001);
		EXPECT_NEAR(bounds.max.y, 1.0F, 0.0001);
	}

	
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionReplaysRotateAndScaleTransformsScale) {
	

	{
		qec::EditorSession session = selected_box_session();
		qec::RepeatLastActionState state;
		qec::record_repeatable_transform(state, session, scale_drag({ 2.0F, 1.0F, 1.0F }), false);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		EXPECT_TRUE(repeat.ok);
		EXPECT_TRUE(repeat.changed);
		const qec::Bounds3 bounds = session.document_bounds();
		EXPECT_NEAR(bounds.min.x, -2.0F, 0.0001);
		EXPECT_NEAR(bounds.max.x, 2.0F, 0.0001);
	}
}

TEST(QdrModelingRuntimeRepeatLastActionTests, RepeatLastActionRecordsFaceExtrudeAsCurrentSelectionForwardDistance) {
	qec::EditorSession session = selected_box_session();
	select_first_face(session);
	const std::size_t original_face_count = session.objects().front().document.faces.size();
	qec::RepeatLastActionState state;
        qec::record_repeatable_transform(
            state, session,
            move_drag({0.0F, 0.0F, -1.5F},
                      qec::TransformDragOperation::Extrude),
            false);

        const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

	EXPECT_TRUE(repeat.ok);
	EXPECT_TRUE(repeat.changed);
	EXPECT_TRUE(only_object(session).document.faces.size() > original_face_count);
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <editor/modeling/operations/operation_command.h>
#include <editor/modeling/runtime/repeat_last_action.h>

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
	REQUIRE(create.ok);
	REQUIRE(create.changed);
	REQUIRE(session.objects().size() == 1U);
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
	REQUIRE(face != quader_poly::invalid_element_id);

	qec::PolygonObject object;
	object.id = 1;
	object.selected = true;
	object.document = std::move(document);
	qec::EditorSession session({ object });
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::face;
	selection.faces = { face };
	selection.has_active = true;
	selection.active_kind = quader_poly::ElementKind::face;
	selection.active_face = face;
	session.set_component_selection(object.id, selection, qec::SelectionMode::face);
	return session;
}

qec::EditorOperationCommand selection_command(qec::EditorOperationId operation_id)
{
	qec::EditorOperationCommand command;
	command.operation_id = operation_id;
	command.phase = qec::EditorOperationCommandPhase::execute;
	command.settings = qec::SelectionOperationSettings {};
	return command;
}

qec::EditorOperationCommand duplicate_command(quader::QVec3 offset)
{
	qec::DuplicateMeshesOperationSettings settings;
	settings.explicit_offset = { offset, true };
	qec::EditorOperationCommand command;
	command.operation_id = qec::EditorOperationId::duplicate_meshes;
	command.phase = qec::EditorOperationCommandPhase::execute;
	command.settings = settings;
	return command;
}

qec::EditorOperationCommand thicken_command(double thickness, bool from_center = true)
{
	qec::ThickenOperationSettings settings;
	settings.thickness = thickness;
	settings.from_center = from_center;
	qec::EditorOperationCommand command;
	command.operation_id = qec::EditorOperationId::thicken;
	command.phase = qec::EditorOperationCommandPhase::execute;
	command.settings = settings;
	return command;
}

void select_first_face(qec::EditorSession& session)
{
	const qec::PolygonObject& object = session.objects().front();
	REQUIRE_FALSE(object.document.faces.empty());
	const quader_poly::ElementId selected_face = object.document.faces.front().id;
	quader_poly::Selection selection;
	selection.mode = quader_poly::SelectionMode::face;
	selection.faces = { selected_face };
	selection.has_active = true;
	selection.active_kind = quader_poly::ElementKind::face;
	selection.active_face = selected_face;
	session.set_component_selection(object.id, selection, qec::SelectionMode::face);
}

qec::TransformDragState move_drag(quader::QVec3 delta, qec::TransformDragOperation operation = qec::TransformDragOperation::transform)
{
	qec::TransformDragState drag;
	drag.active = true;
	drag.tool = qec::ViewportInteractionTool::move;
	drag.operation = operation;
	drag.axis = qec::TransformAxis::x;
	drag.move.preview_delta = delta;
	return drag;
}

qec::TransformDragState rotate_drag(double angle_degrees)
{
	qec::TransformDragState drag;
	drag.active = true;
	drag.tool = qec::ViewportInteractionTool::rotate;
	drag.operation = qec::TransformDragOperation::transform;
	drag.axis = qec::TransformAxis::z;
	drag.rotate.preview_angle_degrees = angle_degrees;
	return drag;
}

qec::TransformDragState scale_drag(quader::QVec3 scale, qec::TransformDragOperation operation = qec::TransformDragOperation::transform)
{
	qec::TransformDragState drag;
	drag.active = true;
	drag.tool = qec::ViewportInteractionTool::scale;
	drag.operation = operation;
	drag.axis = qec::TransformAxis::all;
	drag.scale.preview_scale = scale;
	return drag;
}

const qec::PolygonObject& only_object(const qec::EditorSession& session)
{
	REQUIRE(session.objects().size() == 1U);
	return session.objects().front();
}

} // namespace

TEST_CASE("repeat last action reports no prior portable action", "[editor-core][repeat-last]")
{
	qec::EditorSession session = selected_box_session();
	qec::RepeatLastActionState state;

	const qec::CommandResult result = qec::execute_repeat_last_action(session, state);

	CHECK_FALSE(result.ok);
	CHECK_FALSE(result.changed);
	CHECK(result.message == "No repeatable action is available.");
}

TEST_CASE("repeat last action ignores non-repeatable delete commands", "[editor-core][repeat-last]")
{
	qec::EditorSession session = selected_box_session();
	qec::RepeatLastActionState state;
	const qec::EditorOperationCommand command = selection_command(qec::EditorOperationId::delete_selection);
	const qec::EditorOperationCommandResult result = qec::execute_command(session, command);
	REQUIRE(result.command.ok);
	REQUIRE(result.command.changed);

	qec::record_repeatable_operation(state, command, result.command);
	const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

	CHECK_FALSE(qec::operation_is_repeatable(qec::EditorOperationId::delete_selection));
	CHECK_FALSE(repeat.ok);
	CHECK_FALSE(repeat.changed);
	CHECK(repeat.message == "No repeatable action is available.");
}

TEST_CASE("repeat last action replays captured operation commands", "[editor-core][repeat-last]")
{
	qec::EditorSession session = selected_box_session();
	qec::RepeatLastActionState state;
	const qec::EditorOperationCommand command = selection_command(qec::EditorOperationId::flip_face_normals);

	const qec::EditorOperationCommandResult first = qec::execute_command(session, command);
	REQUIRE(first.command.ok);
	REQUIRE(first.command.changed);
	qec::record_repeatable_operation(state, command, first.command);

	const qec::EditorOperationCommandResult second = qec::execute_command(session, command);
	REQUIRE(second.command.ok);
	REQUIRE(second.command.changed);
	const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

	CHECK(repeat.ok);
	CHECK(repeat.changed);
	CHECK(session.history_status().undo_count == 3U);
}

TEST_CASE("repeat last action keeps the prior action after failed or unchanged operation results", "[editor-core][repeat-last]")
{
	SECTION("failed")
	{
		qec::EditorSession session = selected_box_session();
		qec::RepeatLastActionState state;
		const qec::EditorOperationCommand previous = duplicate_command({ 2.0F, 0.0F, 0.0F });
		const qec::EditorOperationCommandResult first = qec::execute_command(session, previous);
		REQUIRE(first.command.ok);
		REQUIRE(first.command.changed);
		qec::record_repeatable_operation(state, previous, first.command);

		qec::EditorOperationCommand failed = selection_command(qec::EditorOperationId::flip_face_normals);
		failed.settings = qec::BridgeOperationSettings {};
		const qec::EditorOperationCommandResult failed_result = qec::execute_command(session, failed);
		REQUIRE_FALSE(failed_result.command.ok);
		qec::record_repeatable_operation(state, failed, failed_result.command);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		REQUIRE(repeat.ok);
		REQUIRE(repeat.changed);
		CHECK(session.objects().size() == 3U);
	}

	SECTION("unchanged")
	{
		qec::EditorSession session = selected_box_session();
		qec::RepeatLastActionState state;
		const qec::EditorOperationCommand previous = duplicate_command({ 2.0F, 0.0F, 0.0F });
		const qec::EditorOperationCommandResult first = qec::execute_command(session, previous);
		REQUIRE(first.command.ok);
		REQUIRE(first.command.changed);
		qec::record_repeatable_operation(state, previous, first.command);

		const qec::EditorOperationCommand ignored = selection_command(qec::EditorOperationId::flip_face_normals);
		qec::record_repeatable_operation(state, ignored, { true, false, {} });

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		REQUIRE(repeat.ok);
		REQUIRE(repeat.changed);
		CHECK(session.objects().size() == 3U);
	}
}

TEST_CASE("repeat last action promotes preview settings only after changed commits", "[editor-core][repeat-last]")
{
	SECTION("changed commit captures the pending execute settings")
	{
		qec::EditorSession session = selected_quad_face_session();
		const std::size_t original_face_count = session.objects().front().document.faces.size();
		qec::RepeatLastActionState state;

		qec::EditorOperationCommand command = thicken_command(0.25);
		command.phase = qec::EditorOperationCommandPhase::begin_preview;
		const qec::EditorOperationCommandResult begin = qec::execute_command(session, command);
		REQUIRE(begin.command.ok);
		REQUIRE(begin.command.changed);
		qec::record_repeatable_operation(state, command, begin.command);

		command = thicken_command(0.5);
		command.phase = qec::EditorOperationCommandPhase::update_preview;
		const qec::EditorOperationCommandResult update = qec::execute_command(session, command);
		REQUIRE(update.command.ok);
		REQUIRE(update.command.changed);
		qec::record_repeatable_operation(state, command, update.command);

		command.phase = qec::EditorOperationCommandPhase::commit_preview;
		const qec::EditorOperationCommandResult commit = qec::execute_command(session, command);
		REQUIRE(commit.command.ok);
		REQUIRE(commit.command.changed);
		qec::record_repeatable_operation(state, command, commit.command);

		const std::size_t committed_face_count = session.objects().front().document.faces.size();
		REQUIRE(committed_face_count > original_face_count);

		REQUIRE(state.last_action_kind == qec::RepeatableActionKind::operation);
		REQUIRE(state.last_operation.has_value());
		CHECK_FALSE(state.pending_preview_operation.has_value());
		CHECK(state.last_operation->operation_id == qec::EditorOperationId::thicken);
		CHECK(state.last_operation->phase == qec::EditorOperationCommandPhase::execute);
		const qec::ThickenOperationSettings stored =
			std::get<qec::ThickenOperationSettings>(state.last_operation->settings);
		CHECK(stored.thickness == Catch::Approx(0.5));
	}

	SECTION("cancel clears pending settings")
	{
		qec::EditorSession session = selected_quad_face_session();
		qec::RepeatLastActionState state;

		qec::EditorOperationCommand command = thicken_command(0.25);
		command.phase = qec::EditorOperationCommandPhase::begin_preview;
		const qec::EditorOperationCommandResult begin = qec::execute_command(session, command);
		REQUIRE(begin.command.ok);
		qec::record_repeatable_operation(state, command, begin.command);

		command.phase = qec::EditorOperationCommandPhase::cancel_preview;
		const qec::EditorOperationCommandResult cancel = qec::execute_command(session, command);
		REQUIRE(cancel.command.ok);
		qec::record_repeatable_operation(state, command, cancel.command);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		CHECK_FALSE(repeat.ok);
		CHECK_FALSE(repeat.changed);
		CHECK(repeat.message == "No repeatable action is available.");
	}
}

TEST_CASE("repeat last action replays duplicate move transforms", "[editor-core][repeat-last]")
{
	qec::EditorSession session = selected_box_session();
	const qec::Bounds3 original_bounds = session.document_bounds();
	qec::RepeatLastActionState state;
	qec::record_repeatable_transform(state, session, move_drag({ 2.0F, 0.0F, 0.0F }), true);

	const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

	REQUIRE(repeat.ok);
	REQUIRE(repeat.changed);
	CHECK(session.objects().size() == 2U);
	const qec::Bounds3 moved_bounds = session.document_bounds();
	CHECK(moved_bounds.max.x == Catch::Approx(original_bounds.max.x + 2.0F));
	CHECK(session.history_status().undo_count == 1U);
}

TEST_CASE("repeat last action replays rotate and scale transforms", "[editor-core][repeat-last]")
{
	SECTION("rotate")
	{
		qec::EditorSession session = selected_box_session({ -1.0F, -2.0F, -0.5F }, { 1.0F, 2.0F, 0.5F });
		qec::RepeatLastActionState state;
		qec::record_repeatable_transform(state, session, rotate_drag(90.0), false);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		REQUIRE(repeat.ok);
		REQUIRE(repeat.changed);
		const qec::Bounds3 bounds = session.document_bounds();
		CHECK(bounds.min.x == Catch::Approx(-2.0F));
		CHECK(bounds.max.x == Catch::Approx(2.0F));
		CHECK(bounds.min.y == Catch::Approx(-1.0F));
		CHECK(bounds.max.y == Catch::Approx(1.0F));
	}

	SECTION("scale")
	{
		qec::EditorSession session = selected_box_session();
		qec::RepeatLastActionState state;
		qec::record_repeatable_transform(state, session, scale_drag({ 2.0F, 1.0F, 1.0F }), false);

		const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

		REQUIRE(repeat.ok);
		REQUIRE(repeat.changed);
		const qec::Bounds3 bounds = session.document_bounds();
		CHECK(bounds.min.x == Catch::Approx(-2.0F));
		CHECK(bounds.max.x == Catch::Approx(2.0F));
	}
}

TEST_CASE("repeat last action records face extrude as current selection forward distance", "[editor-core][repeat-last]")
{
	qec::EditorSession session = selected_box_session();
	select_first_face(session);
	const std::size_t original_face_count = session.objects().front().document.faces.size();
	qec::RepeatLastActionState state;
	qec::record_repeatable_transform(
		state,
		session,
		move_drag({ 0.0F, 0.0F, -1.5F }, qec::TransformDragOperation::extrude),
		false);

	const qec::CommandResult repeat = qec::execute_repeat_last_action(session, state);

	REQUIRE(repeat.ok);
	REQUIRE(repeat.changed);
	CHECK(only_object(session).document.faces.size() > original_face_count);
}

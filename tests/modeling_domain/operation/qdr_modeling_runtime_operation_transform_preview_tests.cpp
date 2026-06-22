////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/interactions/interaction_api.h>

#include <cstdint>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;
	camera.transform.origin = { 0.0F, 0.0F, 6.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

} // namespace

TEST_CASE("transform preview transactions restore, commit, and validate the active kind", "[operation][transform_preview]")
{
	SECTION("cancel restores the base state without recording undo")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_editor_core::Bounds3 original_bounds = session.document_bounds();
		const std::uint64_t original_content_revision = session.state().content_revision;
		const std::uint64_t original_selection_revision = session.state().selection_revision;

		const quader_editor_core::CommandResult begin = session.begin_transform_preview(false);
		const quader_editor_core::CommandResult update = session.update_transform_translate_preview({ 1.0F, 0.0F, 0.0F });

		REQUIRE(begin.ok);
		REQUIRE_FALSE(begin.changed);
		REQUIRE(update.ok);
		REQUIRE(update.changed);
		const quader_editor_core::PreviewTransactionStatus active_status = session.preview_transaction_status();
		REQUIRE(active_status.active);
		REQUIRE(active_status.kind == quader_editor_core::PreviewTransactionKind::transform);
		REQUIRE(active_status.changed);
		const quader_editor_core::Bounds3 preview_bounds = session.document_bounds();
		REQUIRE(preview_bounds.min.x == Catch::Approx(original_bounds.min.x + 1.0F));
		REQUIRE_FALSE(session.history_status().can_undo);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);

		REQUIRE(cancel.before.active);
		REQUIRE(cancel.before.changed);
		REQUIRE(cancel.restored);
		REQUIRE_FALSE(session.preview_transaction_status().active);
		const quader_editor_core::Bounds3 canceled_bounds = session.document_bounds();
		REQUIRE(canceled_bounds.min.x == Catch::Approx(original_bounds.min.x));
		REQUIRE(session.state().content_revision == original_content_revision);
		REQUIRE(session.state().selection_revision == original_selection_revision);
		REQUIRE_FALSE(session.history_status().can_undo);
	}

	SECTION("commit records one undo step")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_editor_core::Bounds3 original_bounds = session.document_bounds();

		REQUIRE(session.begin_transform_preview(false).ok);
		REQUIRE(session.update_transform_translate_preview({ 0.0F, 2.0F, 0.0F }).changed);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);

		REQUIRE(commit.before.active);
		REQUIRE(commit.before.changed);
		REQUIRE_FALSE(commit.restored);
		REQUIRE_FALSE(session.preview_transaction_status().active);
		const quader_editor_core::HistoryStatus history = session.history_status();
		REQUIRE(history.can_undo);
		REQUIRE_FALSE(history.can_redo);
		REQUIRE(history.undo_count == 1U);
		const quader_editor_core::Bounds3 committed_bounds = session.document_bounds();
		REQUIRE(committed_bounds.min.y == Catch::Approx(original_bounds.min.y + 2.0F));

		const quader_editor_core::HistoryRestoreResult undo = session.undo();
		REQUIRE(undo.applied);
		REQUIRE(undo.status.can_redo);
		const quader_editor_core::Bounds3 undone_bounds = session.document_bounds();
		REQUIRE(undone_bounds.min.y == Catch::Approx(original_bounds.min.y));

		const quader_editor_core::HistoryRestoreResult redo = session.redo();
		REQUIRE(redo.applied);
		REQUIRE(redo.status.can_undo);
		const quader_editor_core::Bounds3 redone_bounds = session.document_bounds();
		REQUIRE(redone_bounds.min.y == Catch::Approx(original_bounds.min.y + 2.0F));
	}

	SECTION("duplicate preview remains transactional")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const std::size_t original_object_count = session.objects().size();

		const quader_editor_core::CommandResult begin_duplicate = session.begin_transform_preview(true);

		REQUIRE(begin_duplicate.ok);
		REQUIRE(begin_duplicate.changed);
		REQUIRE(session.objects().size() == original_object_count + 1U);
		REQUIRE(session.preview_transaction_status().changed);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);

		REQUIRE(cancel.restored);
		REQUIRE(session.objects().size() == original_object_count);
		REQUIRE_FALSE(session.history_status().can_undo);
	}

	SECTION("mismatched finish leaves the active transaction intact")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		REQUIRE(session.begin_transform_preview(false).ok);

		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		REQUIRE(commit.before.active);
		REQUIRE_FALSE(commit.restored);
		REQUIRE(session.preview_transaction_status().active);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);

		REQUIRE(cancel.before.active);
		REQUIRE(cancel.restored);
		REQUIRE_FALSE(session.preview_transaction_status().active);
	}

	SECTION("scene transform drag preview updates drag state without mutating the session")
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_editor_core::Bounds3 original_bounds = session.document_bounds();
		const std::uint64_t original_content_revision = session.state().content_revision;
		const std::uint64_t original_selection_revision = session.state().selection_revision;

		REQUIRE(session.begin_transform_preview(false).ok);

		quader_editor_core::MoveBeginInput begin_input;
		begin_input.axis = quader_editor_core::TransformAxis::x;
		begin_input.center_local = {};
		begin_input.has_world_center = true;
		begin_input.world_center = {};
		begin_input.has_screen_axis = true;
		begin_input.screen_pivot = { 100.0F, 100.0F };
		begin_input.screen_tip = { 200.0F, 100.0F };
		begin_input.screen_position = { 100.0F, 100.0F };
		const quader_editor_core::MoveBeginResult begin = quader_editor_core::begin_move_drag(begin_input);
		REQUIRE(begin.ok);

		quader_editor_core::TransformDragState drag;
		drag.active = true;
		drag.tool = quader_editor_core::ViewportInteractionTool::move;
		drag.operation = quader_editor_core::TransformDragOperation::transform;
		drag.axis = quader_editor_core::TransformAxis::x;
		drag.scene_transform_preview_eligible = true;
		drag.move = begin.state;

		quader_editor_core::TransformDragPreviewInput input;
		input.camera = front_camera();
		input.screen_position = { 160.0F, 100.0F };
		input.precision = true;
		const quader_editor_core::CommandResult preview =
			quader_editor_core::apply_transform_drag_preview(session, drag, input);

		REQUIRE(preview.ok);
		REQUIRE(preview.changed);
		CHECK(drag.move.preview_delta.x != Catch::Approx(0.0F));
		CHECK_FALSE(session.preview_transaction_status().changed);
		CHECK(session.state().content_revision == original_content_revision);
		CHECK(session.state().selection_revision == original_selection_revision);
		const quader_editor_core::Bounds3 current_bounds = session.document_bounds();
		CHECK(current_bounds.min.x == Catch::Approx(original_bounds.min.x));
		CHECK(current_bounds.max.x == Catch::Approx(original_bounds.max.x));
	}
}

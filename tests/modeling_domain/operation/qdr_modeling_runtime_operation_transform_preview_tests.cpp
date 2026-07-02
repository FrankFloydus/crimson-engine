////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/interactions/interaction_api.hpp>

#include <cstdint>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
        camera.transform.origin = { 0.0F, 0.0F, 6.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

} // namespace

TEST(QdrModelingRuntimeOperationTransformPreviewTests, TransformPreviewTransactionsRestoreCommitAndValidateTheActiveKindCancelRestoresTheBaseStateWithoutRecordingUndo) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_editor_core::Bounds3 original_bounds = session.document_bounds();
		const std::uint64_t original_content_revision = session.state().content_revision;
		const std::uint64_t original_selection_revision = session.state().selection_revision;

		const quader_editor_core::CommandResult begin = session.begin_transform_preview(false);
		const quader_editor_core::CommandResult update = session.update_transform_translate_preview({ 1.0F, 0.0F, 0.0F });

		EXPECT_TRUE(begin.ok);
		EXPECT_FALSE(begin.changed);
		EXPECT_TRUE(update.ok);
		EXPECT_TRUE(update.changed);
		const quader_editor_core::PreviewTransactionStatus active_status = session.preview_transaction_status();
		EXPECT_TRUE(active_status.active);
                EXPECT_TRUE(
                    active_status.kind ==
                    quader_editor_core::PreviewTransactionKind::Transform);
                EXPECT_TRUE(active_status.changed);
		const quader_editor_core::Bounds3 preview_bounds = session.document_bounds();
		EXPECT_NEAR(preview_bounds.min.x, original_bounds.min.x + 1.0F, 0.0001);
		EXPECT_FALSE(session.history_status().can_undo);

                const quader_editor_core::PreviewTransactionFinishResult
                    cancel = session.cancel_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::Transform);

                EXPECT_TRUE(cancel.before.active);
		EXPECT_TRUE(cancel.before.changed);
		EXPECT_TRUE(cancel.restored);
		EXPECT_FALSE(session.preview_transaction_status().active);
		const quader_editor_core::Bounds3 canceled_bounds = session.document_bounds();
		EXPECT_NEAR(canceled_bounds.min.x, original_bounds.min.x, 0.0001);
		EXPECT_TRUE(session.state().content_revision == original_content_revision);
		EXPECT_TRUE(session.state().selection_revision == original_selection_revision);
		EXPECT_FALSE(session.history_status().can_undo);
	}

	

	

	

	
}

TEST(QdrModelingRuntimeOperationTransformPreviewTests, TransformPreviewTransactionsRestoreCommitAndValidateTheActiveKindCommitRecordsOneUndoStep) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_editor_core::Bounds3 original_bounds = session.document_bounds();

		EXPECT_TRUE(session.begin_transform_preview(false).ok);
		EXPECT_TRUE(session.update_transform_translate_preview({ 0.0F, 2.0F, 0.0F }).changed);
                const quader_editor_core::PreviewTransactionFinishResult
                    commit = session.commit_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::Transform);

                EXPECT_TRUE(commit.before.active);
		EXPECT_TRUE(commit.before.changed);
		EXPECT_FALSE(commit.restored);
		EXPECT_FALSE(session.preview_transaction_status().active);
		const quader_editor_core::HistoryStatus history = session.history_status();
		EXPECT_TRUE(history.can_undo);
		EXPECT_FALSE(history.can_redo);
		EXPECT_TRUE(history.undo_count == 1U);
		const quader_editor_core::Bounds3 committed_bounds = session.document_bounds();
		EXPECT_NEAR(committed_bounds.min.y, original_bounds.min.y + 2.0F, 0.0001);

		const quader_editor_core::HistoryRestoreResult undo = session.undo();
		EXPECT_TRUE(undo.applied);
		EXPECT_TRUE(undo.status.can_redo);
		const quader_editor_core::Bounds3 undone_bounds = session.document_bounds();
		EXPECT_NEAR(undone_bounds.min.y, original_bounds.min.y, 0.0001);

		const quader_editor_core::HistoryRestoreResult redo = session.redo();
		EXPECT_TRUE(redo.applied);
		EXPECT_TRUE(redo.status.can_undo);
		const quader_editor_core::Bounds3 redone_bounds = session.document_bounds();
		EXPECT_NEAR(redone_bounds.min.y, original_bounds.min.y + 2.0F, 0.0001);
	}

	

	

	
}

TEST(QdrModelingRuntimeOperationTransformPreviewTests, TransformPreviewTransactionsRestoreCommitAndValidateTheActiveKindDuplicatePreviewRemainsTransactional) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const std::size_t original_object_count = session.objects().size();

		const quader_editor_core::CommandResult begin_duplicate = session.begin_transform_preview(true);

		EXPECT_TRUE(begin_duplicate.ok);
		EXPECT_TRUE(begin_duplicate.changed);
		EXPECT_TRUE(session.objects().size() == original_object_count + 1U);
		EXPECT_TRUE(session.preview_transaction_status().changed);

                const quader_editor_core::PreviewTransactionFinishResult
                    cancel = session.cancel_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::Transform);

                EXPECT_TRUE(cancel.restored);
		EXPECT_TRUE(session.objects().size() == original_object_count);
		EXPECT_FALSE(session.history_status().can_undo);
	}

	

	
}

TEST(QdrModelingRuntimeOperationTransformPreviewTests, TransformPreviewTransactionsRestoreCommitAndValidateTheActiveKindMismatchedFinishLeavesTheActiveTransactionIntact) {
	

	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		EXPECT_TRUE(session.begin_transform_preview(false).ok);

                const quader_editor_core::PreviewTransactionFinishResult
                    commit = session.commit_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::Thicken);

                EXPECT_TRUE(commit.before.active);
		EXPECT_FALSE(commit.restored);
		EXPECT_TRUE(session.preview_transaction_status().active);

                const quader_editor_core::PreviewTransactionFinishResult
                    cancel = session.cancel_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::Transform);

                EXPECT_TRUE(cancel.before.active);
		EXPECT_TRUE(cancel.restored);
		EXPECT_FALSE(session.preview_transaction_status().active);
	}

	
}

TEST(QdrModelingRuntimeOperationTransformPreviewTests, TransformPreviewTransactionsRestoreCommitAndValidateTheActiveKindSceneTransformDragPreviewUpdatesDragStateWithoutMutatingTheSession) {
	

	

	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		const quader_editor_core::Bounds3 original_bounds = session.document_bounds();
		const std::uint64_t original_content_revision = session.state().content_revision;
		const std::uint64_t original_selection_revision = session.state().selection_revision;

		EXPECT_TRUE(session.begin_transform_preview(false).ok);

		quader_editor_core::MoveBeginInput begin_input;
                begin_input.axis = quader_editor_core::TransformAxis::X;
                begin_input.center_local = {};
		begin_input.has_world_center = true;
		begin_input.world_center = {};
		begin_input.has_screen_axis = true;
		begin_input.screen_pivot = { 100.0F, 100.0F };
		begin_input.screen_tip = { 200.0F, 100.0F };
		begin_input.screen_position = { 100.0F, 100.0F };
		const quader_editor_core::MoveBeginResult begin = quader_editor_core::begin_move_drag(begin_input);
		EXPECT_TRUE(begin.ok);

		quader_editor_core::TransformDragState drag;
		drag.active = true;
                drag.tool = quader_editor_core::ViewportInteractionTool::Move;
                drag.operation =
                    quader_editor_core::TransformDragOperation::Transform;
                drag.axis = quader_editor_core::TransformAxis::X;
                drag.scene_transform_preview_eligible = true;
		drag.move = begin.state;

		quader_editor_core::TransformDragPreviewInput input;
		input.camera = front_camera();
		input.screen_position = { 160.0F, 100.0F };
		input.precision = true;
		const quader_editor_core::CommandResult preview =
			quader_editor_core::apply_transform_drag_preview(session, drag, input);

		EXPECT_TRUE(preview.ok);
		EXPECT_TRUE(preview.changed);
		EXPECT_FALSE(qdr_test::nearly_equal(drag.move.preview_delta.x, 0.0F, 0.0001));
		EXPECT_FALSE(session.preview_transaction_status().changed);
		EXPECT_TRUE(session.state().content_revision == original_content_revision);
		EXPECT_TRUE(session.state().selection_revision == original_selection_revision);
		const quader_editor_core::Bounds3 current_bounds = session.document_bounds();
		EXPECT_NEAR(current_bounds.min.x, original_bounds.min.x, 0.0001);
		EXPECT_NEAR(current_bounds.max.x, original_bounds.max.x, 0.0001);
	}
}

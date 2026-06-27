////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_schema.hpp>

TEST(QdrModelingRuntimeOperationSliceQuadTests, SliceQuadOperationSchemaOwnsIntegerSliceRanges) {
	const quader_editor_core::SliceQuadOperationSchema schema = quader_editor_core::slice_quad_operation_schema();

	EXPECT_TRUE(schema.id == quader_editor_core::EditorOperationId::slice_quad);
	EXPECT_TRUE(std::string(schema.operation_id) == "slice_quad");
	EXPECT_TRUE(std::string(schema.label) == "Slice Quad");
	EXPECT_TRUE(std::string(schema.x_slices.id) == "x_slices");
	EXPECT_TRUE(std::string(schema.y_slices.id) == "y_slices");
	EXPECT_TRUE(schema.x_slices.default_value == 1);
	EXPECT_TRUE(schema.y_slices.default_value == 1);
	EXPECT_TRUE(schema.x_slices.minimum == 0);
	EXPECT_TRUE(schema.y_slices.minimum == 0);
	EXPECT_TRUE(schema.x_slices.maximum == 64);
	EXPECT_TRUE(schema.y_slices.maximum == 64);
	EXPECT_TRUE(schema.x_slices.hard_limits);
	EXPECT_TRUE(schema.y_slices.hard_limits);

	quader_editor_core::SliceQuadOperationSettings settings;
	settings.x_slices = -100;
	settings.y_slices = 1000;
	const quader_editor_core::SliceQuadOperationSettings normalized =
		quader_editor_core::normalize_slice_quad_operation_settings(settings);
	EXPECT_TRUE(normalized.x_slices == 0);
	EXPECT_TRUE(normalized.y_slices == 64);
}

TEST(QdrModelingRuntimeOperationSliceQuadTests, SliceQuadPreviewsGeneratedFaceGridsTransactionallyCommitRecordsGeneratedTopologyAndUndoRestoresTheBase) {
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		EXPECT_TRUE(session.begin_slice_quad_preview(1, 1).changed);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::slice_quad);
		EXPECT_TRUE(session.objects().front().document.faces.size() == 4U);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::slice_quad);
		EXPECT_TRUE(commit.before.changed);
		EXPECT_TRUE(session.history_status().can_undo);
		EXPECT_TRUE(session.undo().applied);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
	}

	

	
}

TEST(QdrModelingRuntimeOperationSliceQuadTests, SliceQuadPreviewsGeneratedFaceGridsTransactionallyCancelAndUpdateRebuildFromThePreviewBase) {
	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);

		EXPECT_TRUE(session.begin_slice_quad_preview(1, 0).changed);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::slice_quad);
		EXPECT_TRUE(session.objects().front().document.faces.size() == 2U);
		EXPECT_TRUE(session.update_slice_quad_preview(2, 1).changed);
		EXPECT_TRUE(session.objects().front().document.faces.size() == 6U);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::slice_quad);

		EXPECT_TRUE(cancel.restored);
		EXPECT_TRUE(session.objects().front().document.faces.size() == 1U);
		EXPECT_FALSE(session.history_status().can_undo);
	}

	
}

TEST(QdrModelingRuntimeOperationSliceQuadTests, SliceQuadPreviewsGeneratedFaceGridsTransactionallyWrongExpectedKindDoesNotFinishTheActiveSliceQuadPreview) {
	

	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);
		EXPECT_TRUE(session.begin_slice_quad_preview(1, 1).changed);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		EXPECT_FALSE(cancel.restored);
		EXPECT_TRUE(session.preview_transaction_status().active);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::slice_quad);
	}
}

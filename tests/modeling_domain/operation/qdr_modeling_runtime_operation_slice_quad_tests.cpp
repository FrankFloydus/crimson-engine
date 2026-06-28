////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_schema.hpp>

namespace {

quader_poly::ElementId find_face_with_normal(const quader_poly::Document& document, quader::QVec3 direction)
{
	const auto face = std::ranges::find_if(document.faces, [&document, direction](const quader_poly::Face& candidate) {
		return dot(face_normal(document, candidate), direction) > 0.5F;
	});
	return face == document.faces.end() ? quader_poly::invalid_element_id : face->id;
}

} // namespace

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

TEST(QdrModelingRuntimeOperationSliceQuadTests, SliceQuadPreviewBaseCanBeRebuiltFromAChangedFaceSelectionBeforeExplicitFinish) {
	quader_editor_core::PolygonObject object = qdr_test::make_object(1, quader_poly::make_cube({}));
	quader_editor_core::EditorSession session({ object });
	const quader_poly::ElementId top_face =
		find_face_with_normal(session.objects().front().document, { 0.0F, 1.0F, 0.0F });
	const quader_poly::ElementId right_face =
		find_face_with_normal(session.objects().front().document, { 1.0F, 0.0F, 0.0F });
	ASSERT_TRUE(top_face != quader_poly::invalid_element_id);
	ASSERT_TRUE(right_face != quader_poly::invalid_element_id);
	session.set_component_selection(
		1,
		qdr_test::face_selection({ top_face }, top_face),
		quader_editor_core::SelectionMode::face);

	ASSERT_TRUE(session.begin_slice_quad_preview(1, 0).changed);
	EXPECT_TRUE(session.preview_transaction_status().active);
	EXPECT_TRUE(session.objects().front().document.faces.size() == 7U);

	ASSERT_TRUE(session.restore_preview_transaction_base_for_edit(
		quader_editor_core::PreviewTransactionKind::slice_quad));
	EXPECT_TRUE(session.objects().front().document.faces.size() == 6U);
	session.set_component_selection(
		1,
		qdr_test::face_selection({ right_face }, right_face),
		quader_editor_core::SelectionMode::face);
	ASSERT_TRUE(session.capture_preview_transaction_base_after_edit(
		quader_editor_core::PreviewTransactionKind::slice_quad));
	ASSERT_TRUE(session.update_slice_quad_preview(1, 0).changed);

	EXPECT_TRUE(session.preview_transaction_status().active);
	EXPECT_TRUE(session.objects().front().document.faces.size() == 7U);
	EXPECT_TRUE(qdr_test::selected_face_count(session) == 2U);

	const quader_editor_core::PreviewTransactionFinishResult cancel =
		session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::slice_quad);
	EXPECT_TRUE(cancel.restored);
	EXPECT_FALSE(session.preview_transaction_status().active);
	EXPECT_TRUE(session.objects().front().document.faces.size() == 6U);
	EXPECT_TRUE(qdr_test::selected_face_count(session) == 1U);
	EXPECT_TRUE(session.objects().front().selection.active_face == top_face);
}

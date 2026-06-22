////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/modeling/operations/operation_schema.h>

TEST_CASE("slice quad operation schema owns integer slice ranges", "[operation][slice_quad]")
{
	const quader_editor_core::SliceQuadOperationSchema schema = quader_editor_core::slice_quad_operation_schema();

	REQUIRE(schema.id == quader_editor_core::EditorOperationId::slice_quad);
	REQUIRE(std::string(schema.operation_id) == "slice_quad");
	REQUIRE(std::string(schema.label) == "Slice Quad");
	REQUIRE(std::string(schema.x_slices.id) == "x_slices");
	REQUIRE(std::string(schema.y_slices.id) == "y_slices");
	REQUIRE(schema.x_slices.default_value == 1);
	REQUIRE(schema.y_slices.default_value == 1);
	REQUIRE(schema.x_slices.minimum == 0);
	REQUIRE(schema.y_slices.minimum == 0);
	REQUIRE(schema.x_slices.maximum == 64);
	REQUIRE(schema.y_slices.maximum == 64);
	REQUIRE(schema.x_slices.hard_limits);
	REQUIRE(schema.y_slices.hard_limits);

	quader_editor_core::SliceQuadOperationSettings settings;
	settings.x_slices = -100;
	settings.y_slices = 1000;
	const quader_editor_core::SliceQuadOperationSettings normalized =
		quader_editor_core::normalize_slice_quad_operation_settings(settings);
	REQUIRE(normalized.x_slices == 0);
	REQUIRE(normalized.y_slices == 64);
}

TEST_CASE("slice quad previews generated face grids transactionally", "[operation][slice_quad]")
{
	SECTION("commit records generated topology and undo restores the base")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		REQUIRE(session.begin_slice_quad_preview(1, 1).changed);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::slice_quad);
		REQUIRE(session.objects().front().document.faces.size() == 4U);
		const quader_editor_core::PreviewTransactionFinishResult commit =
			session.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::slice_quad);
		REQUIRE(commit.before.changed);
		REQUIRE(session.history_status().can_undo);
		REQUIRE(session.undo().applied);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before);
	}

	SECTION("cancel and update rebuild from the preview base")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);

		REQUIRE(session.begin_slice_quad_preview(1, 0).changed);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::slice_quad);
		REQUIRE(session.objects().front().document.faces.size() == 2U);
		REQUIRE(session.update_slice_quad_preview(2, 1).changed);
		REQUIRE(session.objects().front().document.faces.size() == 6U);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::slice_quad);

		REQUIRE(cancel.restored);
		REQUIRE(session.objects().front().document.faces.size() == 1U);
		REQUIRE_FALSE(session.history_status().can_undo);
	}

	SECTION("wrong expected kind does not finish the active slice quad preview")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_quad_document().document);
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(
			1,
			qdr_test::face_selection({ session.objects().front().document.faces.front().id }, session.objects().front().document.faces.front().id),
			quader_editor_core::SelectionMode::face);
		REQUIRE(session.begin_slice_quad_preview(1, 1).changed);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		REQUIRE_FALSE(cancel.restored);
		REQUIRE(session.preview_transaction_status().active);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::slice_quad);
	}
}

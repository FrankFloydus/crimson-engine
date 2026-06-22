////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.h"

#include <editor/modeling/operations/operation_schema.h>

TEST_CASE("bridge operation schema owns defaults ranges and normalizer", "[operation][bridge]")
{
	const quader_editor_core::BridgeOperationSchema bridge = quader_editor_core::bridge_operation_schema();

	REQUIRE(bridge.id == quader_editor_core::EditorOperationId::bridge);
	REQUIRE(std::string(bridge.operation_id) == "bridge");
	REQUIRE(std::string(bridge.label) == "Bridge");
	REQUIRE(std::string(bridge.steps.id) == "steps");
	REQUIRE(bridge.steps.default_value == 1);
	REQUIRE(bridge.steps.minimum == 1);
	REQUIRE(bridge.steps.maximum == 64);
	REQUIRE(bridge.steps.step == 1);
	REQUIRE(bridge.steps.hard_limits);
	REQUIRE(quader_editor_core::normalize_bridge_operation_settings({ -100 }).steps == 1);
	REQUIRE(quader_editor_core::normalize_bridge_operation_settings({ 1000 }).steps == 64);
}

TEST_CASE("bridge operation previews from base topology without committing history early", "[operation][bridge]")
{
	SECTION("face bridge preview rebuilds from base and cancels without history")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::all_face_selection(session.objects().front().document), quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		REQUIRE(session.begin_bridge_preview(1).changed);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::bridge);
		REQUIRE(session.objects().front().document.faces.size() > face_count_before);
		REQUIRE(session.update_bridge_preview(3).changed);
		REQUIRE(session.objects().front().document.faces.size() > face_count_before);
		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::bridge);
		REQUIRE(cancel.restored);
		REQUIRE(session.objects().front().document.faces.size() == face_count_before);
		REQUIRE_FALSE(session.history_status().can_undo);
	}

	SECTION("schema settings are normalized before bridge preview")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::all_face_selection(session.objects().front().document), quader_editor_core::SelectionMode::face);

		quader_editor_core::BridgeOperationSettings settings;
		settings.steps = 1000;
		const quader_editor_core::CommandResult begin = session.begin_bridge_preview(settings);
		settings.steps = -100;
		const quader_editor_core::CommandResult update = session.update_bridge_preview(settings);

		REQUIRE(begin.ok);
		REQUIRE(begin.changed);
		REQUIRE(update.ok);
		REQUIRE(update.changed);
		REQUIRE(session.preview_transaction_status().active);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::bridge);
	}

	SECTION("wrong expected kind does not finish the active bridge preview")
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::all_face_selection(session.objects().front().document), quader_editor_core::SelectionMode::face);
		REQUIRE(session.begin_bridge_preview(1).changed);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		REQUIRE_FALSE(cancel.restored);
		REQUIRE(session.preview_transaction_status().active);
		REQUIRE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::bridge);
	}
}

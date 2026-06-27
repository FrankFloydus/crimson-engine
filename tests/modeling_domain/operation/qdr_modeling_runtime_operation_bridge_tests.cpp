////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_schema.hpp>

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationSchemaOwnsDefaultsRangesAndNormalizer) {
	const quader_editor_core::BridgeOperationSchema bridge = quader_editor_core::bridge_operation_schema();

	EXPECT_TRUE(bridge.id == quader_editor_core::EditorOperationId::bridge);
	EXPECT_TRUE(std::string(bridge.operation_id) == "bridge");
	EXPECT_TRUE(std::string(bridge.label) == "Bridge");
	EXPECT_TRUE(std::string(bridge.steps.id) == "steps");
	EXPECT_TRUE(bridge.steps.default_value == 1);
	EXPECT_TRUE(bridge.steps.minimum == 1);
	EXPECT_TRUE(bridge.steps.maximum == 64);
	EXPECT_TRUE(bridge.steps.step == 1);
	EXPECT_TRUE(bridge.steps.hard_limits);
	EXPECT_TRUE(quader_editor_core::normalize_bridge_operation_settings({ -100 }).steps == 1);
	EXPECT_TRUE(quader_editor_core::normalize_bridge_operation_settings({ 1000 }).steps == 64);
}

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationPreviewsFromBaseTopologyWithoutCommittingHistoryEarlyFaceBridgePreviewRebuildsFromBaseAndCancelsWithoutHistory) {
	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::all_face_selection(session.objects().front().document), quader_editor_core::SelectionMode::face);
		const std::size_t face_count_before = session.objects().front().document.faces.size();

		EXPECT_TRUE(session.begin_bridge_preview(1).changed);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::bridge);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
		EXPECT_TRUE(session.update_bridge_preview(3).changed);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::bridge);
		EXPECT_TRUE(cancel.restored);
		EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
		EXPECT_FALSE(session.history_status().can_undo);
	}

	

	
}

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationPreviewsFromBaseTopologyWithoutCommittingHistoryEarlySchemaSettingsAreNormalizedBeforeBridgePreview) {
	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::all_face_selection(session.objects().front().document), quader_editor_core::SelectionMode::face);

		quader_editor_core::BridgeOperationSettings settings;
		settings.steps = 1000;
		const quader_editor_core::CommandResult begin = session.begin_bridge_preview(settings);
		settings.steps = -100;
		const quader_editor_core::CommandResult update = session.update_bridge_preview(settings);

		EXPECT_TRUE(begin.ok);
		EXPECT_TRUE(begin.changed);
		EXPECT_TRUE(update.ok);
		EXPECT_TRUE(update.changed);
		EXPECT_TRUE(session.preview_transaction_status().active);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::bridge);
	}

	
}

TEST(QdrModelingRuntimeOperationBridgeTests, BridgeOperationPreviewsFromBaseTopologyWithoutCommittingHistoryEarlyWrongExpectedKindDoesNotFinishTheActiveBridgePreview) {
	

	

	{
		quader_editor_core::PolygonObject object = qdr_test::make_object(1, qdr_test::make_parallel_quad_document());
		quader_editor_core::EditorSession session({ object });
		session.set_component_selection(1, qdr_test::all_face_selection(session.objects().front().document), quader_editor_core::SelectionMode::face);
		EXPECT_TRUE(session.begin_bridge_preview(1).changed);

		const quader_editor_core::PreviewTransactionFinishResult cancel =
			session.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::thicken);

		EXPECT_FALSE(cancel.restored);
		EXPECT_TRUE(session.preview_transaction_status().active);
		EXPECT_TRUE(session.preview_transaction_status().kind == quader_editor_core::PreviewTransactionKind::bridge);
	}
}

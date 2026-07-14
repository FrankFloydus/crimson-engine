////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

#include <editor/modeling/operations/operation_schema.hpp>

#include <limits>

using namespace quader_poly;

TEST(QdrModelingRuntimeOperationEdgeBevelTests, EdgeBevelOperationSchemaNormalizesNonFiniteAndOutOfRangeSettings) {
	const quader_editor_core::EdgeBevelOperationSchema bevel = quader_editor_core::edge_bevel_operation_schema();

        EXPECT_TRUE(bevel.id ==
                    quader_editor_core::EditorOperationId::EdgeBevel);
        EXPECT_TRUE(std::string(bevel.operation_id) == "edge_bevel");
	EXPECT_TRUE(std::string(bevel.width.id) == "width");
	EXPECT_TRUE(std::string(bevel.profile.id) == "profile");
	EXPECT_TRUE(std::string(bevel.segments.id) == "segments");
	EXPECT_NEAR(bevel.width.default_value, 1.0, 0.0001);
	EXPECT_NEAR(bevel.width.minimum, 0.0, 0.0001);
	EXPECT_NEAR(bevel.width.maximum, 512.0, 0.0001);
	EXPECT_NEAR(bevel.width.step, 0.001, 0.0001);
	EXPECT_NEAR(bevel.profile.default_value, 0.5, 0.0001);
	EXPECT_TRUE(bevel.segments.default_value == 1);
	EXPECT_TRUE(bevel.segments.maximum == 16);
        EXPECT_TRUE(bevel.default_profile_type ==
                    quader_poly::BevelProfileType::Offset);

        quader_editor_core::EdgeBevelOperationSettings settings;
	settings.width = std::numeric_limits<double>::quiet_NaN();
	settings.profile = std::numeric_limits<double>::infinity();
	settings.profile_type = -100;
	settings.segments = -2;
	const quader_editor_core::EdgeBevelOperationSettings normalized =
		quader_editor_core::normalize_edge_bevel_operation_settings(settings);

	EXPECT_NEAR(normalized.width, bevel.width.default_value, 0.0001);
	EXPECT_NEAR(normalized.profile, bevel.profile.default_value, 0.0001);
	EXPECT_TRUE(normalized.segments == bevel.segments.minimum);
        EXPECT_TRUE(normalized.profile_type ==
                    static_cast<int>(quader_poly::BevelProfileType::Offset));

	const quader_editor_core::EdgeBevelOperationSettings low =
		quader_editor_core::normalize_edge_bevel_operation_settings(
			{ -1.0, 0.5, 0, 1 });
	EXPECT_NEAR(low.width, 0.0, 0.0001);

        const quader_editor_core::EdgeBevelOperationSettings high =
		quader_editor_core::normalize_edge_bevel_operation_settings({ 20000.0, 2.0, 999, 999 });
	EXPECT_NEAR(high.width, bevel.width.maximum, 0.0001);
	EXPECT_NEAR(high.profile, bevel.profile.maximum, 0.0001);
	EXPECT_TRUE(high.segments == bevel.segments.maximum);
}

TEST(QdrModelingRuntimeOperationEdgeBevelTests, EdgeBevelOperationPreviewsFromBaseTopologyAndCommitsThroughHistoryEdgeBevelPreviewRebuildsFromBaseAndCommitsToHistory) {
	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		quader_editor_core::PolygonObject& object = session.objects().front();
		const Edge edge = document_edges(object.document).front();
                session.set_component_selection(
                    object.id, qdr_test::edge_selection({edge}, edge),
                    quader_editor_core::SelectionMode::Edge);
                const std::size_t face_count_before = object.document.faces.size();

		EdgeBevelSettings settings;
		settings.width = 0.1F;
		settings.segments = 2;
		EXPECT_TRUE(session.begin_edge_bevel_preview(settings).changed);
                EXPECT_TRUE(
                    session.preview_transaction_status().kind ==
                    quader_editor_core::PreviewTransactionKind::EdgeBevel);
                settings.width = 0.2F;
		EXPECT_TRUE(session.update_edge_bevel_preview(settings).changed);
		EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
                const quader_editor_core::PreviewTransactionFinishResult
                    commit = session.commit_preview_transaction(
                        quader_editor_core::PreviewTransactionKind::EdgeBevel);
                EXPECT_TRUE(commit.before.changed);
		EXPECT_TRUE(session.history_status().can_undo);
	}

	

	
}

TEST(QdrModelingRuntimeOperationEdgeBevelTests, EdgeBevelOperationPreviewsFromBaseTopologyAndCommitsThroughHistorySchemaSettingsClampInvalidBevelRangesForPreview) {
	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();
		quader_editor_core::PolygonObject& object = session.objects().front();
		const Edge edge = document_edges(object.document).front();
                session.set_component_selection(
                    object.id, qdr_test::edge_selection({edge}, edge),
                    quader_editor_core::SelectionMode::Edge);

                quader_editor_core::EdgeBevelOperationSettings settings;
		settings.width = -10.0;
		settings.profile = 20.0;
		settings.segments = 1000;
		const quader_editor_core::CommandResult begin = session.begin_edge_bevel_preview(settings);

		EXPECT_TRUE(begin.ok);
		EXPECT_TRUE(begin.changed);
		EXPECT_TRUE(session.preview_transaction_status().active);
                EXPECT_TRUE(
                    session.preview_transaction_status().kind ==
                    quader_editor_core::PreviewTransactionKind::EdgeBevel);
        }

	
}

TEST(QdrModelingRuntimeOperationEdgeBevelTests, EdgeBevelOperationPreviewsFromBaseTopologyAndCommitsThroughHistoryPreviewWithoutAnEdgeSelectionFailsAndClearsTheTransaction) {
	

	

	{
		quader_editor_core::EditorSession session = qdr_test::selected_cube_session();

		const quader_editor_core::CommandResult begin =
			session.begin_edge_bevel_preview(quader_poly::EdgeBevelSettings {});

		EXPECT_FALSE(begin.ok);
		EXPECT_FALSE(begin.changed);
		EXPECT_FALSE(session.preview_transaction_status().active);
	}
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor_operation_test_helpers.hpp"

using namespace quader_poly;

namespace {

std::vector<KnifePointTarget> quad_knife_points(const qdr_test::QuadFixture& fixture)
{
  return {
      {
          KnifePointTargetKind::ExistingEdge,
          kInvalidElementId,
          make_edge(fixture.bottom_left, fixture.bottom_right),
          0.5F,
          fixture.face,
          {},
      },
      {
          KnifePointTargetKind::FacePoint,
          kInvalidElementId,
          {},
          0.5F,
          fixture.face,
          {0.0F, 0.0F, 0.0F},
      },
      {
          KnifePointTargetKind::ExistingEdge,
          kInvalidElementId,
          make_edge(fixture.top_right, fixture.top_left),
          0.5F,
          fixture.face,
          {},
      },
  };
}

} // namespace

TEST(QdrModelingRuntimeOperationCommitKnifeToolTests, CommitKnifeToolAppliesTheCapturedStrokeThroughEditorHistory) {
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	quader_editor_core::PolygonObject object = qdr_test::make_object(1, fixture.document);
	quader_editor_core::EditorSession session({ object });
	quader_editor_core::KnifeToolState state;
	state.active = true;
	state.has_active_stroke = true;
	state.target_object_id = 1;
	state.points = quad_knife_points(fixture);
	state.point_positions = {
		{ 0.0F, -1.0F, 0.0F },
		{ 0.0F, 0.0F, 0.0F },
		{ 0.0F, 1.0F, 0.0F },
	};
	state.segments = {
		{ 0U, 1U },
		{ 1U, 2U },
	};
	const std::size_t face_count_before = session.objects().front().document.faces.size();

	const quader_editor_core::CommandResult result = session.commit_knife_tool(state);

	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(session.objects().front().document.faces.size() > face_count_before);
	EXPECT_TRUE(session.history_status().can_undo);
	EXPECT_FALSE(quader_editor_core::knife_tool_preview(state).active);
	EXPECT_TRUE(state.points.empty());
	EXPECT_TRUE(state.segments.empty());
	EXPECT_TRUE(session.undo().applied);
	EXPECT_TRUE(session.objects().front().document.faces.size() == face_count_before);
}

TEST(QdrModelingRuntimeOperationCommitKnifeToolTests, CommitKnifeToolRejectsIncompleteOrTargetlessStatePredictablyEmptyStrokeResetsAsUnchangedSuccess) {
	{
		quader_editor_core::EditorSession session;
		session.reset_to_default_scene();
		quader_editor_core::KnifeToolState state;
		state.active = true;

		const quader_editor_core::CommandResult result = session.commit_knife_tool(state);

		EXPECT_TRUE(result.ok);
		EXPECT_FALSE(result.changed);
		EXPECT_FALSE(quader_editor_core::knife_tool_preview(state).active);
	}

	
}

TEST(QdrModelingRuntimeOperationCommitKnifeToolTests, CommitKnifeToolRejectsIncompleteOrTargetlessStatePredictablySegmentsWithoutATargetKeepTheStateInspectableAndInvalid) {
	

	{
		quader_editor_core::EditorSession session;
		session.reset_to_default_scene();
		quader_editor_core::KnifeToolState state;
		state.active = true;
		state.segments = { { 0U, 1U } };
		state.target_object_id = -1;

		const quader_editor_core::CommandResult result = session.commit_knife_tool(state);

		EXPECT_FALSE(result.ok);
		EXPECT_FALSE(result.changed);
                EXPECT_TRUE(state.status ==
                            quader_editor_core::KnifeToolStatus::InvalidTarget);
                EXPECT_TRUE(quader_editor_core::knife_tool_preview(state).active);
	}
}

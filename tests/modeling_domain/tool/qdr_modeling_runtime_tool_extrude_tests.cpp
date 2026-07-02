////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

TEST(QdrModelingRuntimeToolExtrudeTests, ExtrudeDragReportsPreviewModeAndCutCountPolicy) {
	quader_editor_core::ExtrudeBeginInput input;
	input.source_object_id = 4;
	input.source_face_index = 1;
	input.face_normal = { 0.0F, 0.0F, 1.0F };
	input.face_vertices = {
		{ -1.0F, -1.0F, 0.0F },
		{ 1.0F, -1.0F, 0.0F },
		{ 1.0F, 1.0F, 0.0F },
		{ -1.0F, 1.0F, 0.0F },
	};
	input.face_vertex_indices = { 0, 1, 2, 3 };

	const quader_editor_core::ExtrudeStateResult begin = quader_editor_core::begin_extrude_drag(input);
	EXPECT_TRUE(begin.ok);
	quader_editor_core::ExtrudeToolState state = begin.state;

	EXPECT_TRUE(quader_editor_core::preview_extrude_from_distance(state, 3.0, false, true, 1.0).ok);
	EXPECT_NEAR(state.preview_distance, 3.0, 0.0001);
	EXPECT_TRUE(state.max_cut_count == 2);

	const quader_editor_core::ExtrudeStateResult cuts =
		quader_editor_core::adjust_extrude_cut_count(state, 5, true, 1.0);
	EXPECT_TRUE(cuts.ok);
	EXPECT_TRUE(cuts.changed);
	EXPECT_TRUE(cuts.state.requested_cut_count == 2);
	EXPECT_TRUE(cuts.state.active_cut_count == 2);

	quader_editor_core::set_extrude_preview_valid(state, true);
	const quader_editor_core::ExtrudePreviewOperation operation = quader_editor_core::extrude_preview_operation(state);
	EXPECT_TRUE(operation.ok);
        EXPECT_TRUE(operation.preview_mode ==
                    quader_editor_core::ExtrudePreviewMode::Positive);
        EXPECT_FALSE(operation.clear_preview_meshes);

	EXPECT_TRUE(quader_editor_core::preview_extrude_from_distance(state, -1.0, false, true, 1.0).ok);
	const quader_editor_core::ExtrudePreviewOperation negative_operation = quader_editor_core::extrude_preview_operation(state);
        EXPECT_TRUE(negative_operation.preview_mode ==
                    quader_editor_core::ExtrudePreviewMode::Negative);
        EXPECT_TRUE(negative_operation.clear_preview_meshes);
}

TEST(QdrModelingRuntimeToolExtrudeTests, ExtrudeDragPixelsAndGridCutsClampToPreviewCapacity) {
	quader_editor_core::ExtrudeBeginInput input;
	input.source_object_id = 7;
	input.source_face_index = 2;
	input.face_normal = { 0.0F, 1.0F, 0.0F };
	input.face_center = {};
	input.screen_axis = { 0.0F, 1.0F };

	quader_editor_core::ExtrudeStateResult result = quader_editor_core::begin_extrude_drag(input);
	EXPECT_TRUE(result.ok);
	quader_editor_core::ExtrudeToolState state = result.state;

	result = quader_editor_core::preview_extrude_from_drag_pixels(state, 96.0, false, true, 64.0);
	EXPECT_TRUE(result.ok);
	EXPECT_NEAR(result.state.preview_distance, 128.0, 0.0001);
	EXPECT_TRUE(result.state.active_cut_count == 0);

	result = quader_editor_core::adjust_extrude_cut_count(result.state, 5, true, 64.0);
	EXPECT_TRUE(result.ok);
	EXPECT_TRUE(result.changed);
	EXPECT_TRUE(result.state.requested_cut_count == 1);
	EXPECT_TRUE(result.state.active_cut_count == 1);

	const quader_editor_core::ExtrudePreviewStatus status =
		quader_editor_core::extrude_preview_status(result.state, true);
	EXPECT_TRUE(status.show_cut_status);
	EXPECT_TRUE(status.active_cut_count == 1);
	EXPECT_TRUE(status.max_cut_count == 1);
}

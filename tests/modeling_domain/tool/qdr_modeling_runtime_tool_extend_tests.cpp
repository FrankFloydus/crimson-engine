////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/interactions/interaction_api.hpp>

TEST(QdrModelingRuntimeToolExtendTests, ExtendDragUsesFaceNormalAndPrecisionSnapping) {
	quader_editor_core::ExtendBeginInput input;
	input.source_object_id = 7;
	input.source_face_index = 3;
	input.face_center = { 1.0F, 2.0F, 3.0F };
	input.face_normal = { 0.0F, 2.0F, 0.0F };
	input.screen_axis = { 0.0F, -4.0F };

	const quader_editor_core::ExtendStateResult begin = quader_editor_core::begin_extend_drag(input);
	EXPECT_TRUE(begin.ok);
	EXPECT_TRUE(begin.state.active);
	EXPECT_TRUE(begin.state.source_object_id == 7);
	EXPECT_TRUE(begin.state.source_face_index == 3);
	EXPECT_NEAR(begin.state.face_normal.y, 1.0F, 0.0001);
	EXPECT_NEAR(begin.state.screen_axis.y, -1.0F, 0.0001);

	quader_editor_core::ExtendToolState state = begin.state;
	const quader_editor_core::ExtendStateResult preview =
		quader_editor_core::preview_extend_from_distance(state, 2.0, true, true, 0.25);
	EXPECT_TRUE(preview.ok);
	EXPECT_TRUE(preview.changed);
	EXPECT_NEAR(preview.state.preview_distance, 0.5, 0.0001);
	EXPECT_NEAR(preview.state.preview_delta.y, 0.5F, 0.0001);
}

TEST(QdrModelingRuntimeToolExtendTests, ExtendDragFallsBackScreenAxesAndBuildsProjectedHandles) {
	quader_editor_core::ExtendBeginInput input;
	input.source_object_id = 6;
	input.source_face_index = 1;
	input.face_normal = { 0.0F, 1.0F, 0.0F };
	input.screen_axis = {};

	quader_editor_core::ExtendStateResult state = quader_editor_core::begin_extend_drag(input);
	EXPECT_TRUE(state.ok);
	EXPECT_NEAR(state.state.screen_axis.x, 1.0F, 0.0001);
	EXPECT_NEAR(state.state.screen_axis.y, 0.0F, 0.0001);

	state = quader_editor_core::preview_extend_from_drag_pixels(state.state, 96.0, false, true, 2.0);
	EXPECT_TRUE(state.ok);
	EXPECT_NEAR(state.state.preview_distance, 4.0, 0.0001);

	EXPECT_FALSE(quader_editor_core::begin_extend_drag({ 6, 1, {}, {}, { 1.0F, 0.0F } }).ok);
	EXPECT_NEAR(quader_editor_core::extend_handle_axis_for_normal({ -0.9F, 0.1F, 0.0F }).x, -1.0F, 0.0001);

	quader_editor_core::ExtendHandlePrimitiveInput handle_input;
	handle_input.normal = { 0.0F, 1.0F, 0.0F };
	handle_input.screen_center = { 100.0F, 120.0F };
	handle_input.has_projected_axis_delta = true;
	handle_input.projected_axis_delta = { 0.0F, -10.0F };

	quader_editor_core::ExtendHandlePrimitives handle =
		quader_editor_core::build_extend_handle_primitives(handle_input);
	EXPECT_TRUE(handle.ok);
	EXPECT_TRUE(handle.stick_visible);
	EXPECT_NEAR(handle.screen_axis.y, -1.0F, 0.0001);
	EXPECT_NEAR(handle.handle_center.y, 64.0F, 0.0001);

	handle_input.normal = { 0.0F, 0.0F, -1.0F };
	handle_input.has_projected_axis_delta = false;
	handle_input.projected_axis_delta = {};
	handle = quader_editor_core::build_extend_handle_primitives(handle_input);
	EXPECT_TRUE(handle.ok);
	EXPECT_FALSE(handle.stick_visible);
	EXPECT_NEAR(handle.handle_center.x, handle_input.screen_center.x, 0.0001);
	EXPECT_NEAR(handle.handle_center.y, handle_input.screen_center.y, 0.0001);
}

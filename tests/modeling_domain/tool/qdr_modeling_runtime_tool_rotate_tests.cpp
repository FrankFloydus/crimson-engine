////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <editor/interactions/interaction_api.h>

TEST_CASE("rotate drag snaps screen angle and builds pivoted transform", "[editor-core][tool][rotate]")
{
	quader_editor_core::RotateBeginInput input;
	input.axis = quader_editor_core::TransformAxis::z;
	input.object_ids = { 1, 2 };
	input.pivot = { 1.0F, 2.0F, 0.0F };
	input.screen_position = { 120.0F, 100.0F };
	input.has_screen_pivot = true;
	input.screen_pivot = { 100.0F, 100.0F };

	const quader_editor_core::RotateBeginResult begin = quader_editor_core::begin_rotate_drag(input);
	REQUIRE(begin.ok);
	quader_editor_core::RotateToolState state = begin.state;

	const quader_editor_core::RotatePreviewResult preview =
		quader_editor_core::preview_rotate_from_screen_position(state, { 100.0F, 80.0F }, 15.0);
	REQUIRE(preview.ok);
	CHECK(preview.preview_angle_degrees == Catch::Approx(90.0));
	CHECK(preview.transform.x_axis.x == Catch::Approx(0.0).margin(0.0001));
	CHECK(preview.transform.x_axis.y == Catch::Approx(1.0).margin(0.0001));
	CHECK(preview.pivoted_transform.origin.x != Catch::Approx(0.0F));
}

TEST_CASE("rotate drag accumulates circular motion beyond half turns", "[editor-core][tool][rotate]")
{
	quader_editor_core::RotateBeginInput input;
	input.axis = quader_editor_core::TransformAxis::z;
	input.screen_position = { 120.0F, 100.0F };
	input.has_screen_pivot = true;
	input.screen_pivot = { 100.0F, 100.0F };

	const quader_editor_core::RotateBeginResult begin = quader_editor_core::begin_rotate_drag(input);
	REQUIRE(begin.ok);
	quader_editor_core::RotateToolState state = begin.state;

	quader_editor_core::RotatePreviewResult preview =
		quader_editor_core::preview_rotate_from_screen_position(state, { 100.0F, 80.0F }, 0.0);
	REQUIRE(preview.ok);
	CHECK(preview.preview_angle_degrees == Catch::Approx(90.0));

	preview = quader_editor_core::preview_rotate_from_screen_position(state, { 80.0F, 100.0F }, 0.0);
	REQUIRE(preview.ok);
	CHECK(preview.preview_angle_degrees == Catch::Approx(180.0));

	preview = quader_editor_core::preview_rotate_from_screen_position(state, { 100.0F, 120.0F }, 0.0);
	REQUIRE(preview.ok);
	CHECK(preview.preview_angle_degrees == Catch::Approx(270.0));
}

TEST_CASE("modal rotate planner clears tiny duplicate previews", "[editor-core][tool][rotate]")
{
	quader_editor_core::ModalTransformBeginPlan begin =
		quader_editor_core::plan_modal_transform_drag_begin({ true, false });
	CHECK_FALSE(begin.ok);
	CHECK_FALSE(begin.request_duplicate);

	begin = quader_editor_core::plan_modal_transform_drag_begin({ true, true });
	CHECK(begin.ok);
	CHECK(begin.request_duplicate);
	CHECK(begin.mark_selection_dirty);

	const quader_editor_core::ModalTransformUpdatePlan update =
		quader_editor_core::plan_modal_transform_drag_update({
			true,
			quader_editor_core::ModalTransformDragTool::rotate,
			true,
			0.0,
			{ 1.0F, 1.0F, 1.0F },
			quader_editor_core::tool_epsilon,
		});
	CHECK(update.handled);
	CHECK(update.preview_ok);
	CHECK_FALSE(update.preview_valid);
	CHECK(update.route == quader_editor_core::ModalTransformPreviewRoute::clear_preview);

	const quader_editor_core::ModalTransformCommitPlan commit =
		quader_editor_core::plan_modal_transform_drag_commit({
			true,
			quader_editor_core::ModalTransformDragTool::rotate,
			false,
			true,
		});
	CHECK(commit.handled);
	CHECK(commit.restore_base_snapshot);
	CHECK(commit.restore_selection);
	CHECK_FALSE(commit.model_changed);
}

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <modeling/interactions/interaction_api.h>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
	camera.projection = quader_editor_core::CameraProjection::perspective;
	camera.transform.origin = { 0.0F, 0.0F, 6.0F };
	camera.transform.x_axis = { 1.0F, 0.0F, 0.0F };
	camera.transform.y_axis = { 0.0F, 1.0F, 0.0F };
	camera.transform.z_axis = { 0.0F, 0.0F, 1.0F };
	camera.viewport_size = { 800.0F, 600.0F };
	camera.fov_degrees = 60.0;
	return camera;
}

} // namespace

TEST_CASE("scale drag snaps against bounds and supports uniform center pick", "[editor-core][tool][scale]")
{
	quader_editor_core::ScaleBeginInput input;
	input.axis = quader_editor_core::TransformAxis::x;
	input.pivot = { 0.0F, 0.0F, 0.0F };
	input.screen_position = { 200.0F, 200.0F };
	input.has_screen_pivot = true;
	input.screen_pivot = { 200.0F, 200.0F };
	input.has_screen_tip = true;
	input.screen_tip = { 260.0F, 200.0F };
	input.bounds.has_bounds = true;
	input.bounds.min = { -1.0F, -1.0F, -1.0F };
	input.bounds.max = { 1.0F, 1.0F, 1.0F };

	const quader_editor_core::ScaleBeginResult begin = quader_editor_core::begin_scale_drag(input);
	REQUIRE(begin.ok);
	quader_editor_core::ScaleToolState state = begin.state;

	const quader_editor_core::ScalePreviewResult preview =
		quader_editor_core::preview_scale_from_screen_position(state, { 300.0F, 200.0F }, true, 0.5);
	REQUIRE(preview.ok);
	CHECK(preview.preview_scale.x > 1.0F);
	CHECK(preview.preview_scale.y == Catch::Approx(1.0F));
	CHECK(preview.preview_scale.z == Catch::Approx(1.0F));

	quader_editor_core::TransformGizmoFrameInput gizmo;
	gizmo.tool = quader_editor_core::ViewportInteractionTool::scale;
	gizmo.has_selection = true;
	gizmo.has_pivot = true;
	gizmo.pivot = {};
	gizmo.camera = front_camera();
	gizmo.gizmo_size_pixels = 72.0;

	const quader_editor_core::TransformGizmoPickHit center_hit =
		quader_editor_core::pick_transform_gizmo(gizmo, { 400.0F, 300.0F });
	REQUIRE(center_hit.hit);
	CHECK(center_hit.axis == quader_editor_core::TransformAxis::all);
}

TEST_CASE("modal scale planner routes non-identity previews and commit names", "[editor-core][tool][scale]")
{
	const quader_editor_core::ModalTransformUpdatePlan update =
		quader_editor_core::plan_modal_transform_drag_update({
			true,
			quader_editor_core::ModalTransformDragTool::scale,
			true,
			0.0,
			{ 1.0F, 1.25F, 1.0F },
			quader_editor_core::tool_epsilon,
		});
	CHECK(update.handled);
	CHECK(update.preview_valid);
	CHECK(update.route == quader_editor_core::ModalTransformPreviewRoute::transform_preview);

	const quader_editor_core::ModalTransformCommitPlan commit =
		quader_editor_core::plan_modal_transform_drag_commit({
			true,
			quader_editor_core::ModalTransformDragTool::scale,
			true,
			false,
		});
	CHECK(commit.model_changed);
	CHECK(commit.route == quader_editor_core::ModalTransformCommitRoute::transform_patch);
	CHECK(commit.undo_action == "Quader Scale Brushes");
}

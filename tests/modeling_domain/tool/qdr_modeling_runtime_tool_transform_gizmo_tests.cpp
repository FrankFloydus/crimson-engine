////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_test_macros.hpp>

#include <modeling/runtime/editor_runtime.h>
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

TEST_CASE("transform gizmo hover helper promotes hits and clears misses", "[editor-core][tool][gizmo]")
{
	quader_editor_core::EditorRuntimeState runtime;
	quader_editor_core::TransformGizmoPickHit hit;
	hit.hit = true;
	hit.axis = quader_editor_core::TransformAxis::z;

	quader_editor_core::set_editor_runtime_gizmo_hover_from_pick(runtime, hit);
	CHECK(runtime.gizmo_hover_axis == quader_editor_core::TransformAxis::z);

	hit.hit = false;
	quader_editor_core::set_editor_runtime_gizmo_hover_from_pick(runtime, hit);
	CHECK(runtime.gizmo_hover_axis == quader_editor_core::TransformAxis::none);
}

} // namespace

TEST_CASE("transform gizmo builds frames only for transform tools", "[editor-core][tool][gizmo]")
{
	quader_editor_core::TransformGizmoFrameInput input;
	input.tool = quader_editor_core::ViewportInteractionTool::move;
	input.has_selection = true;
	input.has_pivot = true;
	input.pivot = {};
	input.camera = front_camera();
	input.gizmo_size_pixels = 80.0;
	input.pick_radius_pixels = 10.0;

	const quader_editor_core::TransformGizmoFrame move_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	REQUIRE(move_frame.info.ok);
	CHECK(move_frame.axes.size() >= 2);
	CHECK(move_frame.planes.size() >= 2);
	CHECK(move_frame.ring_segments.empty());

	input.tool = quader_editor_core::ViewportInteractionTool::rotate;
	const quader_editor_core::TransformGizmoFrame rotate_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	REQUIRE(rotate_frame.info.ok);
	CHECK(rotate_frame.axes.empty());
	CHECK(rotate_frame.planes.empty());
	CHECK(rotate_frame.ring_segments.size() >= 12);

	input.tool = quader_editor_core::ViewportInteractionTool::extrude;
	const quader_editor_core::TransformGizmoFrame extrude_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	CHECK_FALSE(extrude_frame.info.ok);
	CHECK(extrude_frame.axes.empty());
	CHECK(extrude_frame.planes.empty());
	CHECK(extrude_frame.ring_segments.empty());
}

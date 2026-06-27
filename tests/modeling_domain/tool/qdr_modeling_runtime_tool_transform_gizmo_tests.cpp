////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/interaction_api.hpp>

#include <algorithm>
#include <cmath>

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

float screen_distance(quader::QVec2 left, quader::QVec2 right)
{
	const float dx = right.x - left.x;
	const float dy = right.y - left.y;
	return std::sqrt(dx * dx + dy * dy);
}

TEST(QdrModelingRuntimeToolTransformGizmoTests, TransformGizmoHoverHelperPromotesHitsAndClearsMisses) {
	quader_editor_core::EditorRuntimeState runtime;
	quader_editor_core::TransformGizmoPickHit hit;
	hit.hit = true;
	hit.axis = quader_editor_core::TransformAxis::z;

	quader_editor_core::set_editor_runtime_gizmo_hover_from_pick(runtime, hit);
	EXPECT_TRUE(runtime.gizmo_hover_axis == quader_editor_core::TransformAxis::z);

	hit.hit = false;
	quader_editor_core::set_editor_runtime_gizmo_hover_from_pick(runtime, hit);
	EXPECT_TRUE(runtime.gizmo_hover_axis == quader_editor_core::TransformAxis::none);
}

} // namespace

TEST(QdrModelingRuntimeToolTransformGizmoTests, TransformGizmoBuildsFramesOnlyForTransformTools) {
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
	EXPECT_TRUE(move_frame.info.ok);
	EXPECT_TRUE(move_frame.axes.size() >= 2);
	EXPECT_TRUE(move_frame.planes.size() >= 2);
	EXPECT_TRUE(move_frame.ring_segments.empty());
	EXPECT_TRUE(move_frame.trackball_segments.empty());

	const auto xy_plane = std::find_if(move_frame.planes.begin(), move_frame.planes.end(), [](const quader_editor_core::TransformGizmoPlanePrimitive& plane) {
		return plane.axis == quader_editor_core::TransformAxis::xy;
	});
	EXPECT_TRUE(xy_plane != move_frame.planes.end());
	const quader::QVec2 xy_center {
		(xy_plane->screen0.x + xy_plane->screen1.x + xy_plane->screen2.x + xy_plane->screen3.x) * 0.25F,
		(xy_plane->screen0.y + xy_plane->screen1.y + xy_plane->screen2.y + xy_plane->screen3.y) * 0.25F,
	};
	EXPECT_TRUE(screen_distance(move_frame.info.screen_pivot, xy_center) > static_cast<float>(input.gizmo_size_pixels) * 0.60F);

	input.tool = quader_editor_core::ViewportInteractionTool::rotate;
	const quader_editor_core::TransformGizmoFrame rotate_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	EXPECT_TRUE(rotate_frame.info.ok);
	EXPECT_TRUE(rotate_frame.axes.empty());
	EXPECT_TRUE(rotate_frame.planes.empty());
	EXPECT_TRUE(rotate_frame.trackball_segments.size() >= 12);
	EXPECT_TRUE(rotate_frame.ring_segments.size() >= 12);
	float trackball_radius = 0.0F;
	for (const quader_editor_core::TransformGizmoTrackballSegmentPrimitive& segment : rotate_frame.trackball_segments) {
		trackball_radius = std::max(trackball_radius, screen_distance(rotate_frame.info.screen_pivot, segment.screen_start));
		trackball_radius = std::max(trackball_radius, screen_distance(rotate_frame.info.screen_pivot, segment.screen_end));
	}
	float axis_ring_radius = 0.0F;
	for (const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment : rotate_frame.ring_segments) {
		axis_ring_radius = std::max(axis_ring_radius, screen_distance(rotate_frame.info.screen_pivot, segment.screen_start));
		axis_ring_radius = std::max(axis_ring_radius, screen_distance(rotate_frame.info.screen_pivot, segment.screen_end));
	}
	EXPECT_TRUE(trackball_radius > axis_ring_radius * 1.08F);
	EXPECT_TRUE(trackball_radius < axis_ring_radius * 1.20F);
	EXPECT_TRUE(std::any_of(rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(), [](const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment) {
		return segment.front_facing && segment.alpha == 1.0F;
	}));
	EXPECT_TRUE(std::any_of(rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(), [](const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment) {
		return !segment.front_facing && segment.alpha < 1.0F;
	}));

	input.tool = quader_editor_core::ViewportInteractionTool::extrude;
	const quader_editor_core::TransformGizmoFrame extrude_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	EXPECT_FALSE(extrude_frame.info.ok);
	EXPECT_TRUE(extrude_frame.axes.empty());
	EXPECT_TRUE(extrude_frame.planes.empty());
	EXPECT_TRUE(extrude_frame.ring_segments.empty());
	EXPECT_TRUE(extrude_frame.trackball_segments.empty());
}

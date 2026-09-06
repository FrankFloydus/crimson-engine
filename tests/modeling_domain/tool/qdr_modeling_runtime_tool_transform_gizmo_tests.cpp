////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/runtime/editor_runtime.hpp>
#include <editor/interactions/interaction_api.hpp>
#include <editor/interactions/transform_gizmo.hpp>

#include <algorithm>
#include <cmath>

namespace {

quader_editor_core::ViewportCameraState front_camera()
{
	quader_editor_core::ViewportCameraState camera;
        camera.projection = quader_editor_core::CameraProjection::Perspective;
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

const quader_editor_core::TransformGizmoAxisPrimitive* find_axis(
		const quader_editor_core::TransformGizmoFrame& frame,
		quader_editor_core::TransformAxis axis)
{
	const auto found = std::find_if(frame.axes.begin(), frame.axes.end(), [axis](const quader_editor_core::TransformGizmoAxisPrimitive& primitive) {
		return primitive.axis == axis;
	});
	return found == frame.axes.end() ? nullptr : &*found;
}

quader_poly::Transform3 quarter_turn_z_orientation()
{
	quader_poly::Transform3 orientation;
	orientation.x_axis = {0.0F, 1.0F, 0.0F};
	orientation.y_axis = {-1.0F, 0.0F, 0.0F};
	return orientation;
}

TEST(QdrModelingRuntimeToolTransformGizmoTests, TransformGizmoHoverHelperPromotesHitsAndClearsMisses) {
	quader_editor_core::EditorRuntimeState runtime;
	quader_editor_core::TransformGizmoPickHit hit;
	hit.hit = true;
        hit.axis = quader_editor_core::TransformAxis::Z;

        quader_editor_core::set_editor_runtime_gizmo_hover_from_pick(runtime, hit);
        EXPECT_TRUE(runtime.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::Z);

        hit.hit = false;
	quader_editor_core::set_editor_runtime_gizmo_hover_from_pick(runtime, hit);
        EXPECT_TRUE(runtime.gizmo_hover_axis ==
                    quader_editor_core::TransformAxis::None);
}

} // namespace

TEST(QdrModelingRuntimeToolTransformGizmoTests, TransformGizmoBuildsFramesOnlyForTransformTools) {
	quader_editor_core::TransformGizmoFrameInput input;
        input.tool = quader_editor_core::ViewportInteractionTool::Move;
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

        const quader_editor_core::TransformGizmoAxisPrimitive *move_x_axis =
            find_axis(move_frame, quader_editor_core::TransformAxis::X);
        ASSERT_TRUE(move_x_axis != nullptr);
	EXPECT_NEAR(
		screen_distance(move_frame.info.screen_pivot, move_x_axis->screen_tip),
		static_cast<float>(input.gizmo_size_pixels * 1.75),
		1.5F);

        const auto xy_plane = std::find_if(
            move_frame.planes.begin(), move_frame.planes.end(),
            [](const quader_editor_core::TransformGizmoPlanePrimitive &plane) {
              return plane.axis == quader_editor_core::TransformAxis::Xy;
            });
        EXPECT_TRUE(xy_plane != move_frame.planes.end());
	const quader::QVec2 xy_center {
		(xy_plane->screen0.x + xy_plane->screen1.x + xy_plane->screen2.x + xy_plane->screen3.x) * 0.25F,
		(xy_plane->screen0.y + xy_plane->screen1.y + xy_plane->screen2.y + xy_plane->screen3.y) * 0.25F,
	};
	EXPECT_NEAR(
		screen_distance(move_frame.info.screen_pivot, xy_center),
		static_cast<float>(input.gizmo_size_pixels * std::sqrt(2.0) * 0.4),
		1.5F);
	EXPECT_NEAR(
		screen_distance(xy_plane->screen0, xy_plane->screen1),
		static_cast<float>(input.gizmo_size_pixels * 0.2),
		1.0F);

        input.tool = quader_editor_core::ViewportInteractionTool::Scale;
        const quader_editor_core::TransformGizmoFrame scale_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	EXPECT_TRUE(scale_frame.info.ok);
        const quader_editor_core::TransformGizmoAxisPrimitive *scale_x_axis =
            find_axis(scale_frame, quader_editor_core::TransformAxis::X);
        ASSERT_TRUE(scale_x_axis != nullptr);
	EXPECT_NEAR(
		screen_distance(scale_frame.info.screen_pivot, scale_x_axis->screen_tip),
		static_cast<float>(input.gizmo_size_pixels * 1.554),
		1.5F);

        input.tool = quader_editor_core::ViewportInteractionTool::Rotate;
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
	EXPECT_NEAR(axis_ring_radius, static_cast<float>(input.gizmo_size_pixels * 1.1), 2.0F);
	EXPECT_TRUE(trackball_radius > axis_ring_radius * 1.08F);
	EXPECT_TRUE(trackball_radius < axis_ring_radius * 1.20F);
	EXPECT_TRUE(std::any_of(rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(), [](const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment) {
		return segment.front_facing && segment.alpha == 1.0F;
	}));
	EXPECT_TRUE(std::any_of(rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(), [](const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment) {
		return !segment.front_facing && segment.alpha < 1.0F;
	}));

        input.tool = quader_editor_core::ViewportInteractionTool::Extrude;
        const quader_editor_core::TransformGizmoFrame extrude_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	EXPECT_FALSE(extrude_frame.info.ok);
	EXPECT_TRUE(extrude_frame.axes.empty());
	EXPECT_TRUE(extrude_frame.planes.empty());
	EXPECT_TRUE(extrude_frame.ring_segments.empty());
	EXPECT_TRUE(extrude_frame.trackball_segments.empty());
}

TEST(QdrModelingRuntimeToolTransformGizmoTests,
     MoveCenterHandleFacesTheCameraAndWinsAtTheSharedAxisOrigin) {
	constexpr float kInvSqrtTwo = 0.70710678F;
	quader_editor_core::TransformGizmoFrameInput input;
	input.tool = quader_editor_core::ViewportInteractionTool::Move;
	input.has_selection = true;
	input.has_pivot = true;
	input.pivot = {};
	input.camera = front_camera();
	input.camera.transform.origin = {
		6.0F * kInvSqrtTwo, 0.0F, 6.0F * kInvSqrtTwo};
	input.camera.transform.x_axis = {kInvSqrtTwo, 0.0F, -kInvSqrtTwo};
	input.camera.transform.y_axis = {0.0F, 1.0F, 0.0F};
	input.camera.transform.z_axis = {kInvSqrtTwo, 0.0F, kInvSqrtTwo};
	input.gizmo_size_pixels = 80.0;
	input.pick_radius_pixels = 10.0;
	input.move_center_radius_pixels = 8.0;

	const quader_editor_core::TransformGizmoFrame frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	ASSERT_TRUE(frame.info.ok);
	EXPECT_EQ(
		std::count_if(
			frame.planes.begin(), frame.planes.end(),
			[](const quader_editor_core::TransformGizmoPlanePrimitive& plane) {
				return plane.axis == quader_editor_core::TransformAxis::All;
			}),
		1);

	const auto center_plane = std::find_if(
		frame.planes.begin(), frame.planes.end(),
		[](const quader_editor_core::TransformGizmoPlanePrimitive& plane) {
			return plane.axis == quader_editor_core::TransformAxis::All;
		});
	ASSERT_TRUE(center_plane != frame.planes.end());
	const quader::QVec2 center_screen {
		(center_plane->screen0.x + center_plane->screen1.x +
		 center_plane->screen2.x + center_plane->screen3.x) * 0.25F,
		(center_plane->screen0.y + center_plane->screen1.y +
		 center_plane->screen2.y + center_plane->screen3.y) * 0.25F,
	};
	EXPECT_NEAR(center_screen.x, frame.info.screen_pivot.x, 0.0001F);
	EXPECT_NEAR(center_screen.y, frame.info.screen_pivot.y, 0.0001F);
	EXPECT_NEAR(
		screen_distance(center_plane->screen0, center_plane->screen1),
		static_cast<float>(input.move_center_radius_pixels * 2.0), 0.5F);
	EXPECT_NEAR(
		screen_distance(center_plane->screen1, center_plane->screen2),
		static_cast<float>(input.move_center_radius_pixels * 2.0), 0.5F);

	const auto distance_from_view_plane = [&input](quader::QVec3 point) {
		const quader::QVec3 offset {
			point.x - input.pivot.x,
			point.y - input.pivot.y,
			point.z - input.pivot.z,
		};
		return offset.x * input.camera.transform.z_axis.x +
			offset.y * input.camera.transform.z_axis.y +
			offset.z * input.camera.transform.z_axis.z;
	};
	EXPECT_NEAR(distance_from_view_plane(center_plane->world0), 0.0F, 0.0001F);
	EXPECT_NEAR(distance_from_view_plane(center_plane->world1), 0.0F, 0.0001F);
	EXPECT_NEAR(distance_from_view_plane(center_plane->world2), 0.0F, 0.0001F);
	EXPECT_NEAR(distance_from_view_plane(center_plane->world3), 0.0F, 0.0001F);

	ASSERT_FALSE(frame.axes.empty());
	for (const quader_editor_core::TransformGizmoAxisPrimitive& axis : frame.axes) {
		EXPECT_NEAR(axis.screen_start.x, frame.info.screen_pivot.x, 0.0001F);
		EXPECT_NEAR(axis.screen_start.y, frame.info.screen_pivot.y, 0.0001F);
	}
	const quader_editor_core::TransformGizmoPickHit center_hit =
		quader_editor_core::pick_transform_gizmo(input, frame.info.screen_pivot);
	ASSERT_TRUE(center_hit.hit);
	EXPECT_TRUE(center_hit.axis == quader_editor_core::TransformAxis::All);
	EXPECT_NEAR(center_hit.world_direction.x, kInvSqrtTwo, 0.0001F);
	EXPECT_NEAR(center_hit.world_direction.y, 0.0F, 0.0001F);
	EXPECT_NEAR(center_hit.world_direction.z, kInvSqrtTwo, 0.0001F);

	const quader_editor_core::TransformGizmoAxisPrimitive* x_axis =
		find_axis(frame, quader_editor_core::TransformAxis::X);
	ASSERT_TRUE(x_axis != nullptr);
	const quader_editor_core::TransformGizmoPickHit axis_tip_hit =
		quader_editor_core::pick_transform_gizmo(input, x_axis->screen_tip);
	ASSERT_TRUE(axis_tip_hit.hit);
	EXPECT_TRUE(axis_tip_hit.axis == quader_editor_core::TransformAxis::X);
}

TEST(QdrModelingRuntimeToolTransformGizmoTests,
     TransformGizmoBuildsAndPicksHandlesInTheProvidedOrientation) {
	quader_editor_core::TransformGizmoFrameInput input;
	input.tool = quader_editor_core::ViewportInteractionTool::Move;
	input.has_selection = true;
	input.has_pivot = true;
	input.pivot = {};
	input.orientation = quarter_turn_z_orientation();
	input.camera = front_camera();
	input.gizmo_size_pixels = 80.0;
	input.pick_radius_pixels = 6.0;

	const quader_editor_core::TransformGizmoFrame move_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	const quader_editor_core::TransformGizmoAxisPrimitive* x_axis =
		find_axis(move_frame, quader_editor_core::TransformAxis::X);
	ASSERT_TRUE(x_axis != nullptr);
	EXPECT_NEAR(x_axis->world_tip.x, 0.0F, 0.0001F);
	EXPECT_TRUE(x_axis->world_tip.y > 0.0F);
	EXPECT_NEAR(x_axis->world_tip.z, 0.0F, 0.0001F);

	const quader_editor_core::TransformGizmoPickHit axis_hit =
		quader_editor_core::pick_transform_gizmo(input, x_axis->screen_tip);
	ASSERT_TRUE(axis_hit.hit);
	EXPECT_TRUE(axis_hit.axis == quader_editor_core::TransformAxis::X);
	EXPECT_NEAR(axis_hit.world_direction.x, 0.0F, 0.0001F);
	EXPECT_NEAR(axis_hit.world_direction.y, 1.0F, 0.0001F);
	EXPECT_NEAR(axis_hit.world_direction.z, 0.0F, 0.0001F);

	const auto xy_plane = std::find_if(
		move_frame.planes.begin(), move_frame.planes.end(),
		[](const quader_editor_core::TransformGizmoPlanePrimitive& plane) {
			return plane.axis == quader_editor_core::TransformAxis::Xy;
		});
	ASSERT_TRUE(xy_plane != move_frame.planes.end());
	const quader::QVec2 plane_center {
		(xy_plane->screen0.x + xy_plane->screen1.x + xy_plane->screen2.x +
		 xy_plane->screen3.x) * 0.25F,
		(xy_plane->screen0.y + xy_plane->screen1.y + xy_plane->screen2.y +
		 xy_plane->screen3.y) * 0.25F,
	};
	const quader_editor_core::TransformGizmoPickHit plane_hit =
		quader_editor_core::pick_transform_gizmo(input, plane_center);
	ASSERT_TRUE(plane_hit.hit);
	EXPECT_TRUE(plane_hit.axis == quader_editor_core::TransformAxis::Xy);
	EXPECT_NEAR(plane_hit.world_direction.x, -1.0F, 0.0001F);
	EXPECT_NEAR(plane_hit.world_direction.y, 1.0F, 0.0001F);
	EXPECT_NEAR(plane_hit.world_direction.z, 0.0F, 0.0001F);

	input.tool = quader_editor_core::ViewportInteractionTool::Rotate;
	const quader_editor_core::TransformGizmoFrame rotate_frame =
		quader_editor_core::build_transform_gizmo_frame(input);
	bool saw_oriented_x_ring = false;
	for (const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment :
		 rotate_frame.ring_segments) {
		if (segment.axis != quader_editor_core::TransformAxis::X) {
			continue;
		}
		saw_oriented_x_ring = true;
		EXPECT_NEAR(segment.world_start.y, input.pivot.y, 0.0001F);
		EXPECT_NEAR(segment.world_end.y, input.pivot.y, 0.0001F);
	}
	EXPECT_TRUE(saw_oriented_x_ring);

	bool picked_oriented_x_ring = false;
	for (const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment :
		 rotate_frame.ring_segments) {
		if (segment.axis != quader_editor_core::TransformAxis::X) {
			continue;
		}
		const quader::QVec2 midpoint {
			(segment.screen_start.x + segment.screen_end.x) * 0.5F,
			(segment.screen_start.y + segment.screen_end.y) * 0.5F,
		};
		const quader_editor_core::TransformGizmoPickHit ring_hit =
			quader_editor_core::pick_transform_gizmo(input, midpoint);
		if (!ring_hit.hit || ring_hit.axis != quader_editor_core::TransformAxis::X) {
			continue;
		}
		picked_oriented_x_ring = true;
		EXPECT_NEAR(ring_hit.world_direction.x, 0.0F, 0.0001F);
		EXPECT_NEAR(ring_hit.world_direction.y, 1.0F, 0.0001F);
		EXPECT_NEAR(ring_hit.world_direction.z, 0.0F, 0.0001F);
		break;
	}
	EXPECT_TRUE(picked_oriented_x_ring);
}

TEST(QdrModelingRuntimeToolTransformGizmoTests,
     RotationBallPickingTracksProjectionAndGizmoPixelScale) {
  using namespace quader_editor_core;
  for (const CameraProjection projection :
       {CameraProjection::Perspective, CameraProjection::Orthographic}) {
    for (const double pixel_scale : {1.0, 1.5, 2.0}) {
      for (const double gizmo_scale : {0.5, 1.0, 2.0}) {
        TransformGizmoFrameInput input;
        input.tool = ViewportInteractionTool::Rotate;
        input.has_selection = true;
        input.has_pivot = true;
        input.camera = front_camera();
        input.camera.projection = projection;
        input.camera.orthographic_size = 8.0;
        input.gizmo_size_pixels = 80.0 * pixel_scale;
        input.pick_radius_pixels = 6.0 * pixel_scale;
        input.gizmo_scale = gizmo_scale;
        const TransformGizmoFrame frame = build_transform_gizmo_frame(input);
        ASSERT_TRUE(frame.info.ok);
        const float radius =
            static_cast<float>(88.0 * pixel_scale * gizmo_scale);
        const quader::QVec2 interior =
            frame.info.screen_pivot + quader::QVec2{radius * 0.4F, radius * 0.4F};
        const TransformGizmoPickHit ball = pick_transform_gizmo(input, interior);
        ASSERT_TRUE(ball.hit);
        EXPECT_EQ(ball.axis, TransformAxis::All);
        EXPECT_NEAR(screen_distance(ball.screen_tip, frame.info.screen_pivot),
                    radius, 0.001F);
        const quader::QVec2 ring_point =
            frame.info.screen_pivot +
            quader::QVec2{radius * 0.70710678F, radius * 0.70710678F};
        const TransformGizmoPickHit ring = pick_transform_gizmo(input, ring_point);
        ASSERT_TRUE(ring.hit);
        EXPECT_EQ(ring.axis, TransformAxis::Z);
        EXPECT_FALSE(pick_transform_gizmo(
                         input, frame.info.screen_pivot +
                                    quader::QVec2{radius * 1.4F, radius * 1.4F})
                         .hit);
      }
    }
  }
}

TEST(QdrModelingRuntimeToolTransformGizmoTests,
     HiddenRotationArcsDoNotStealTheBallAndVisibleArcsHavePriority) {
  using namespace quader_editor_core;
  TransformGizmoFrameInput input;
  input.tool = ViewportInteractionTool::Rotate;
  input.has_selection = true;
  input.has_pivot = true;
  input.camera = front_camera();
  input.camera.transform.x_axis = {0.70710678F, 0.0F, -0.70710678F};
  input.camera.transform.y_axis = {-0.40824829F, 0.81649658F, -0.40824829F};
  input.camera.transform.z_axis = {0.57735027F, 0.57735027F, 0.57735027F};
  input.camera.transform.origin = {3.46410162F, 3.46410162F, 3.46410162F};
  input.gizmo_size_pixels = 120.0;
  input.pick_radius_pixels = 8.0;
  const TransformGizmoFrame frame = build_transform_gizmo_frame(input);
  ASSERT_TRUE(frame.info.ok);
  int hidden_arc_ball_hits = 0;
  int visible_arc_hits = 0;
  for (const TransformGizmoRingSegmentPrimitive &segment : frame.ring_segments) {
    const quader::QVec2 midpoint =
        (segment.screen_start + segment.screen_end) * 0.5F;
    const TransformGizmoPickHit hit = pick_transform_gizmo(input, midpoint);
    ASSERT_TRUE(hit.hit);
    if (segment.front_facing) {
      EXPECT_NE(hit.axis, TransformAxis::All);
      ++visible_arc_hits;
    } else {
      const bool near_visible_arc = std::any_of(
          frame.ring_segments.begin(), frame.ring_segments.end(),
          [&](const TransformGizmoRingSegmentPrimitive &other) {
            return other.front_facing &&
                   distance_to_screen_segment_squared(
                       midpoint, other.screen_start, other.screen_end) <= 64.0;
          });
      if (!near_visible_arc) {
        EXPECT_EQ(hit.axis, TransformAxis::All);
        ++hidden_arc_ball_hits;
      }
    }
  }
  EXPECT_GT(visible_arc_hits, 0);
  EXPECT_GT(hidden_arc_ball_hits, 0);
}

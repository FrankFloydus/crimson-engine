////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

using quader::QVec3;
using quader::RenderTransformAxis;
using quader::RenderTransformGizmoRecord;
using quader::RenderTransformTool;
using quader::ViewportOverlayCamera;
using quader::ViewportOverlayFrame;
using quader::ViewportOverlayLayer;
using quader::ViewportTransformGizmoOverlayBuilder;

RenderTransformGizmoRecord rotation_record(float radius = 2.75f) {
  RenderTransformGizmoRecord record;
  record.info.ok = true;
  record.info.tool = RenderTransformTool::Rotate;
  record.info.pivot = {2.0f, -1.0f, 4.0f};
  const QVec3 pivot = record.info.pivot;
  record.ring_segments = {
      {.axis = RenderTransformAxis::X,
       .world_start = pivot + QVec3{0.0f, radius, 0.0f},
       .world_end = pivot + QVec3{0.0f, 0.0f, radius}},
      {.axis = RenderTransformAxis::Y,
       .world_start = pivot + QVec3{radius, 0.0f, 0.0f},
       .world_end = pivot + QVec3{0.0f, 0.0f, radius},
       .front_facing = false},
      {.axis = RenderTransformAxis::Z,
       .world_start = pivot + QVec3{radius, 0.0f, 0.0f},
       .world_end = pivot + QVec3{0.0f, radius, 0.0f}},
  };
  record.trackball_segments = {
      {.world_start = pivot + QVec3{radius * 2.0f, 0.0f, 0.0f},
       .world_end = pivot + QVec3{0.0f, radius * 2.0f, 0.0f}},
  };
  return record;
}

bool has_trackball_surface(const ViewportOverlayFrame &frame) {
  return std::any_of(
      frame.triangles.begin(), frame.triangles.end(),
      [](const quader::ViewportOverlayTriangle &triangle) {
        return triangle.color ==
                   quader::kOverlayTransformGizmoTrackballHoverFillColor ||
               triangle.color ==
                   quader::kOverlayTransformGizmoTrackballActiveFillColor;
      });
}

} // namespace

TEST(ViewportTransformTrackballOverlayTests,
     HoveredSurfaceFollowsCameraAndUsesExistingRingRadiusOnce) {
  const ViewportOverlayCamera camera{
      .forward = {-2.0f, -2.0f, -2.0f},
      .right = {3.0f, 0.0f, -3.0f},
      .up = {-1.0f, 2.0f, -1.0f},
  };
  const QVec3 normal = camera.forward / length(camera.forward);
  for (const float radius : {0.4f, 2.75f, 7.0f}) {
    SCOPED_TRACE(radius);
    RenderTransformGizmoRecord record = rotation_record(radius);
    record.info.hover_axis = RenderTransformAxis::All;
    record.info.gizmo_scale = 1.5;
    ViewportOverlayFrame frame;
    ViewportTransformGizmoOverlayBuilder::append(frame, record, camera);

    ASSERT_GE(frame.triangles.size(), 24u);
    float total_area = 0.0f;
    for (const auto &triangle : frame.triangles) {
      EXPECT_EQ(triangle.color,
                quader::kOverlayTransformGizmoTrackballHoverFillColor);
      EXPECT_EQ(triangle.layer, ViewportOverlayLayer::ToolPreviewFill);
      EXPECT_EQ(triangle.source, quader::ViewportOverlaySource::Tool);
      EXPECT_EQ(triangle.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
      EXPECT_NEAR(length(triangle.a - record.info.pivot), 0.0f, 0.00001f);
      for (const QVec3 point : {triangle.a, triangle.b, triangle.c}) {
        EXPECT_TRUE(std::isfinite(point.x));
        EXPECT_TRUE(std::isfinite(point.y));
        EXPECT_TRUE(std::isfinite(point.z));
        EXPECT_NEAR(dot(point - record.info.pivot, normal), 0.0f, 0.00001f);
      }
      EXPECT_NEAR(length(triangle.b - record.info.pivot), radius, 0.00001f);
      EXPECT_NEAR(length(triangle.c - record.info.pivot), radius, 0.00001f);
      total_area +=
          length(cross(triangle.b - triangle.a, triangle.c - triangle.a)) *
          0.5f;
    }
    EXPECT_NEAR(total_area, 3.14159265358979323846f * radius * radius,
                radius * radius * 0.01f);
    EXPECT_NEAR(length(frame.triangles.front().b - frame.triangles.back().c),
                0.0f, 0.00001f);
  }
}

TEST(ViewportTransformTrackballOverlayTests,
     SurfaceIsSubtleAndKeepsExistingRingsAboveIt) {
  RenderTransformGizmoRecord record = rotation_record();
  ViewportOverlayFrame idle;
  ViewportTransformGizmoOverlayBuilder::append(idle, record, {});
  record.info.hover_axis = RenderTransformAxis::All;
  ViewportOverlayFrame hovered;
  ViewportTransformGizmoOverlayBuilder::append(hovered, record, {});
  record.info.active_axis = RenderTransformAxis::All;
  ViewportOverlayFrame active;
  ViewportTransformGizmoOverlayBuilder::append(active, record, {});

  ASSERT_FALSE(hovered.triangles.empty());
  ASSERT_FALSE(active.triangles.empty());
  EXPECT_EQ(hovered.triangles.front().color,
            quader::rgba8(255, 255, 255, 8));
  EXPECT_EQ(active.triangles.front().color,
            quader::rgba8(255, 255, 255, 12));
  EXPECT_LT(quader::viewport_overlay_draw_order(
                ViewportOverlayLayer::ToolPreviewFill),
            quader::viewport_overlay_draw_order(
                ViewportOverlayLayer::ToolHandle));
  EXPECT_EQ(quader::viewport_overlay_depth_mode(
                ViewportOverlayLayer::ToolPreviewFill,
                quader::ViewportOverlayGeometryKind::Triangles),
            quader::ViewportOverlayDepthMode::DrawOnTop);

  ASSERT_EQ(idle.lines.size(), 3u);
  for (const auto *frame : {&hovered, &active}) {
    ASSERT_EQ(frame->lines.size(), idle.lines.size());
    for (std::size_t index = 0; index < idle.lines.size(); ++index) {
      const auto &line = frame->lines[index];
      const auto &original = idle.lines[index];
      EXPECT_EQ(line.layer, ViewportOverlayLayer::ToolHandle);
      EXPECT_EQ(line.color, original.color);
      EXPECT_FLOAT_EQ(line.width_pixels, original.width_pixels);
      EXPECT_NEAR(length(line.start - original.start), 0.0f, 0.00001f);
      EXPECT_NEAR(length(line.end - original.end), 0.0f, 0.00001f);
    }
  }
}

TEST(ViewportTransformTrackballOverlayTests,
     SurfaceIsAbsentWithoutTrackballHoverOrDrag) {
  for (const auto axis : {RenderTransformAxis::None, RenderTransformAxis::X,
                          RenderTransformAxis::Y, RenderTransformAxis::Z}) {
    RenderTransformGizmoRecord record = rotation_record();
    record.info.hover_axis = axis;
    record.info.active_axis = axis;
    ViewportOverlayFrame frame;
    ViewportTransformGizmoOverlayBuilder::append(frame, record, {});
    EXPECT_FALSE(has_trackball_surface(frame));
  }
  for (const auto tool : {RenderTransformTool::Other, RenderTransformTool::Move,
                          RenderTransformTool::Scale}) {
    RenderTransformGizmoRecord record = rotation_record();
    record.info.tool = tool;
    record.info.hover_axis = RenderTransformAxis::All;
    record.info.active_axis = RenderTransformAxis::All;
    ViewportOverlayFrame frame;
    ViewportTransformGizmoOverlayBuilder::append(frame, record, {});
    EXPECT_FALSE(has_trackball_surface(frame));
  }
}

TEST(ViewportTransformTrackballOverlayTests,
     InvalidCameraOrMissingRadiusDoesNotEmitSurface) {
  RenderTransformGizmoRecord record = rotation_record();
  record.info.hover_axis = RenderTransformAxis::All;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (const auto &camera : std::array{
           ViewportOverlayCamera{.forward = {}},
           ViewportOverlayCamera{.right = {}},
           ViewportOverlayCamera{.forward = {nan, 0.0f, -1.0f}},
           ViewportOverlayCamera{.right = {nan, 0.0f, 0.0f}},
           ViewportOverlayCamera{.right = {0.0f, 0.0f, 2.0f}},
       }) {
    ViewportOverlayFrame frame;
    ViewportTransformGizmoOverlayBuilder::append(frame, record, camera);
    EXPECT_FALSE(has_trackball_surface(frame));
  }

  record.ring_segments.clear();
  ViewportOverlayFrame no_rings;
  ViewportTransformGizmoOverlayBuilder::append(no_rings, record, {});
  EXPECT_FALSE(has_trackball_surface(no_rings));

  record = rotation_record();
  record.info.hover_axis = RenderTransformAxis::All;
  record.ring_segments.front().world_start.x = nan;
  ViewportOverlayFrame invalid_ring;
  ViewportTransformGizmoOverlayBuilder::append(invalid_ring, record, {});
  EXPECT_FALSE(has_trackball_surface(invalid_ring));
}

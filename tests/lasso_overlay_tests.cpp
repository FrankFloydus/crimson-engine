////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace quader {
namespace {

ViewportOverlayCamera lasso_camera() {
  ViewportOverlayCamera camera;
  camera.eye = {0.0f, 0.0f, 10.0f};
  camera.forward = {0.0f, 0.0f, -1.0f};
  camera.right = {1.0f, 0.0f, 0.0f};
  camera.up = {0.0f, 1.0f, 0.0f};
  camera.viewport_size = {800.0f, 600.0f};
  camera.projection = ViewportOverlayProjection::Orthographic;
  camera.orthographic_size = 600.0f;
  return camera;
}

ViewportOverlayFrame lasso_frame(std::span<const QVec2> points,
                                  float raster_scale = 1.0f) {
  ViewportOverlayFrame frame;
  ViewportSelectionMarqueeOverlayBuilder::append(
      frame, lasso_camera(), {.active = true, .lasso_points = points},
      raster_scale);
  return frame;
}

QVec2 to_screen(QVec3 point) {
  return {point.x + 400.0f, 300.0f - point.y};
}

double signed_area_twice(QVec2 a, QVec2 b, QVec2 c) {
  return (double(b.x) - a.x) * (double(c.y) - a.y) -
         (double(b.y) - a.y) * (double(c.x) - a.x);
}

double fill_area(const ViewportOverlayFrame &frame) {
  double area = 0.0;
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    area += std::abs(signed_area_twice(to_screen(triangle.a),
                                      to_screen(triangle.b),
                                      to_screen(triangle.c))) * 0.5;
  }
  return area;
}

bool fills_point(const ViewportOverlayFrame &frame, QVec2 point) {
  return std::any_of(frame.triangles.begin(), frame.triangles.end(),
                     [point](const ViewportOverlayTriangle &triangle) {
    const QVec2 a = to_screen(triangle.a);
    const QVec2 b = to_screen(triangle.b);
    const QVec2 c = to_screen(triangle.c);
    const double ab = signed_area_twice(a, b, point);
    const double bc = signed_area_twice(b, c, point);
    const double ca = signed_area_twice(c, a, point);
    return (ab >= -0.001 && bc >= -0.001 && ca >= -0.001) ||
           (ab <= 0.001 && bc <= 0.001 && ca <= 0.001);
  });
}

/** Records actual coordinator submissions, independently of its source stamp. */
class LassoOverlayTarget final : public ViewportOverlaySubmissionTarget {
public:
  ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return {true, true};
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
                                 ViewportOverlayFrame,
                                 ViewportOverlayCamera) override {
    ++persistent_submissions;
  }
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
                              ViewportOverlayFrame frame,
                              ViewportOverlayCamera) override {
    ++dynamic_submissions;
    dynamic_frame = std::move(frame);
  }

  std::uint64_t persistent_submissions = 0u;
  std::uint64_t dynamic_submissions = 0u;
  ViewportOverlayFrame dynamic_frame;
};

/** Counts entry to the actual lasso projection and tessellation path. */
class CountingLassoSource final : public ViewportOverlayDynamicToolFrameProvider {
public:
  explicit CountingLassoSource(ViewportToolOverlaySourceInputs inputs)
      : source(inputs) {}

  ViewportOverlayFrame build_dynamic_tool_overlay_frame() const override {
    ++builds;
    return source.build_dynamic_tool_overlay_frame();
  }

  ViewportToolOverlaySource source;
  mutable std::uint64_t builds = 0u;
};

TEST(LassoOverlayTests, ClosesLoopWithContinuousAlternatingScreenSpaceDashes) {
  const std::vector<QVec2> points{
      {100.0f, 100.0f}, {300.0f, 100.0f}, {300.0f, 300.0f}, {100.0f, 300.0f}};
  const ViewportOverlayFrame frame = lasso_frame(points);
  ASSERT_FALSE(frame.lines.empty());
  EXPECT_NEAR(fill_area(frame), 40000.0, 0.02);
  EXPECT_NEAR(to_screen(frame.lines.front().start).x, points.front().x, 0.001f);
  EXPECT_NEAR(to_screen(frame.lines.front().start).y, points.front().y, 0.001f);
  EXPECT_NEAR(to_screen(frame.lines.back().end).x, points.front().x, 0.001f);
  EXPECT_NEAR(to_screen(frame.lines.back().end).y, points.front().y, 0.001f);
  double outline_length = 0.0;
  for (std::size_t index = 0; index < frame.lines.size(); ++index) {
    const ViewportOverlayLine &line = frame.lines[index];
    const QVec2 delta = to_screen(line.end) - to_screen(line.start);
    const double length = std::hypot(delta.x, delta.y);
    outline_length += length;
    EXPECT_NEAR(length, 1.0, 0.0001);
    EXPECT_EQ(line.color, index % 2U == 0U ? kOverlaySelectionLassoDarkLineColor
                                          : kOverlaySelectionLassoLightLineColor);
    EXPECT_FLOAT_EQ(line.width_pixels, kOverlaySelectionLassoLineWidthPixels);
    EXPECT_EQ(line.layer, ViewportOverlayLayer::ToolPreviewLine);
    EXPECT_EQ(line.source, ViewportOverlaySource::Tool);
    if (index > 0U) {
      EXPECT_NEAR(length_squared(line.start - frame.lines[index - 1U].end),
                  0.0f, 0.0001f);
    }
  }
  EXPECT_NEAR(outline_length, 800.0, 0.001);
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    EXPECT_EQ(triangle.color, kOverlaySelectionLassoFillColor);
    EXPECT_EQ(triangle.layer, ViewportOverlayLayer::ToolPreviewFill);
    EXPECT_EQ(triangle.source, ViewportOverlaySource::Tool);
  }
}

TEST(LassoOverlayTests, RasterScaleChangesDashLengthWithoutChangingRegion) {
  const std::vector<QVec2> points{
      {100.0f, 100.0f}, {300.0f, 100.0f}, {300.0f, 300.0f}, {100.0f, 300.0f}};
  const auto normal = lasso_frame(points);
  const auto scaled = lasso_frame(points, 2.0f);
  EXPECT_EQ(scaled.lines.size() * 2U, normal.lines.size());
  EXPECT_NEAR(fill_area(scaled), fill_area(normal), 0.001);
  for (const auto &line : scaled.lines) {
    EXPECT_NEAR(std::sqrt(length_squared(line.end - line.start)), 2.0f, 0.0001f);
  }
}

TEST(LassoOverlayTests, ConcaveLassoDoesNotFillItsExteriorNotch) {
  const std::vector<QVec2> points{
      {100.0f, 100.0f}, {300.0f, 100.0f}, {300.0f, 200.0f},
      {200.0f, 200.0f}, {200.0f, 300.0f}, {100.0f, 300.0f}};
  const auto frame = lasso_frame(points);
  EXPECT_NEAR(fill_area(frame), 30000.0, 0.02);
  EXPECT_TRUE(fills_point(frame, {150.0f, 250.0f}));
  EXPECT_TRUE(fills_point(frame, {250.0f, 150.0f}));
  EXPECT_FALSE(fills_point(frame, {250.0f, 250.0f}));
}

TEST(LassoOverlayTests, SelfCrossingLassoFillsBothOddParityLobes) {
  const std::vector<QVec2> points{
      {100.0f, 100.0f}, {300.0f, 300.0f}, {100.0f, 300.0f}, {300.0f, 100.0f}};
  const auto frame = lasso_frame(points);
  EXPECT_NEAR(fill_area(frame), 20000.0, 0.02);
  EXPECT_TRUE(fills_point(frame, {200.0f, 130.0f}));
  EXPECT_TRUE(fills_point(frame, {200.0f, 270.0f}));
  EXPECT_FALSE(fills_point(frame, {130.0f, 200.0f}));
  EXPECT_FALSE(fills_point(frame, {270.0f, 200.0f}));
}

TEST(LassoOverlayTests, NestedLoopsPreserveAnEvenParityHole) {
  const std::vector<QVec2> points{
      {100.0f, 100.0f}, {300.0f, 100.0f}, {300.0f, 300.0f},
      {100.0f, 300.0f}, {100.0f, 100.0f}, {150.0f, 150.0f},
      {250.0f, 150.0f}, {250.0f, 250.0f}, {150.0f, 250.0f},
      {150.0f, 150.0f}, {100.0f, 100.0f}};
  const auto frame = lasso_frame(points);
  EXPECT_NEAR(fill_area(frame), 30000.0, 0.02);
  EXPECT_TRUE(fills_point(frame, {125.0f, 200.0f}));
  EXPECT_FALSE(fills_point(frame, {200.0f, 200.0f}));
}

TEST(LassoOverlayTests, DegenerateAndRepeatedPathsDoNotPaintSpuriousFill) {
  const std::vector<QVec2> line{{100.0f, 100.0f}, {100.0f, 100.0f},
                               {200.0f, 200.0f}};
  EXPECT_TRUE(lasso_frame(line).triangles.empty());
  const std::vector<QVec2> twice{
      {100.0f, 100.0f}, {300.0f, 100.0f}, {300.0f, 300.0f}, {100.0f, 300.0f},
      {100.0f, 100.0f}, {300.0f, 100.0f}, {300.0f, 300.0f}, {100.0f, 300.0f}};
  EXPECT_TRUE(lasso_frame(twice).triangles.empty());
  const std::vector<QVec2> invalid{{100.0f, 100.0f},
                                  {std::numeric_limits<float>::quiet_NaN(), 200.0f},
                                  {200.0f, 100.0f}};
  EXPECT_TRUE(lasso_frame(invalid).lines.empty());
  EXPECT_TRUE(lasso_frame(invalid).triangles.empty());
}

TEST(LassoOverlayTests, ProjectsToTheSamePixelsInPerspectiveAndOrthographicViews) {
  const std::vector<QVec2> points{{123.0f, 154.0f}, {347.0f, 182.0f},
                                 {211.0f, 401.0f}};
  for (const auto projection : {ViewportOverlayProjection::Orthographic,
                                ViewportOverlayProjection::Perspective}) {
    auto camera = lasso_camera();
    camera.projection = projection;
    camera.eye = {8.0f, 6.0f, 10.0f};
    camera.forward = normalize_or_zero(camera.eye * -1.0f);
    camera.right = normalize_or_zero(cross(camera.forward, {0.0f, 1.0f, 0.0f}));
    camera.up = cross(camera.right, camera.forward);
    const OverlayCameraProjector projector(camera);
    ViewportOverlayFrame frame;
    ViewportSelectionMarqueeOverlayBuilder::append(
        frame, camera, {.active = true, .lasso_points = points});
    ASSERT_FALSE(frame.lines.empty());
    const auto projected = projector.project(projector.view_point_for(frame.lines.front().start));
    EXPECT_NEAR(projected.pixel.x, points.front().x, 0.01f);
    EXPECT_NEAR(camera.viewport_size.y - projected.pixel.y, points.front().y, 0.01f);
  }
}

TEST(LassoOverlayTests, RetainsUnchangedGeometryAndUpdatesForPathCameraAndBinding) {
  auto camera = lasso_camera();
  ViewportSelectionMarquee marquee;
  marquee.active = true;
  marquee.lasso_points = {{100.0f, 100.0f}, {300.0f, 100.0f},
                          {300.0f, 300.0f}, {100.0f, 300.0f}};
  marquee.path_revision = 1u;
  ViewportToolOverlaySourceInputs inputs{
      .source_identity = 410u,
      .overlay_camera = &camera,
      .selection_marquee = &marquee,
      .interaction_pane = true,
      .overlays_visible = true,
  };
  ViewportOverlaySubmissionCoordinator coordinator;
  LassoOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  std::uint64_t total_builds = 0u;
  const auto submit = [&] {
    const CountingLassoSource provider(inputs);
    const auto result = coordinator.submit(
        {.layer_mask = 1u,
         .raster_scale = inputs.raster_scale,
         .camera = camera,
         .dynamic_tool_source = provider.source.source_stamp(),
         .dynamic_tool_frame_provider = &provider,
         .overlays_visible = true},
        settings, target);
    total_builds += provider.builds;
    return result;
  };
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 1u);
  const double initial_area = fill_area(target.dynamic_frame);
  for (int frame = 0; frame < 4; ++frame) {
    EXPECT_FALSE(submit().dynamic_submitted);
  }
  EXPECT_EQ(total_builds, 1u);
  EXPECT_EQ(target.dynamic_submissions, 1u);

  // Neither endpoint changes: the owner revision must invalidate an interior edit.
  marquee.lasso_points[1] = {250.0f, 120.0f};
  ++marquee.path_revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_LT(fill_area(target.dynamic_frame), initial_area);
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 2u);
  EXPECT_EQ(target.persistent_submissions, 1u);

  camera.eye.x += 2.0f;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 3u);
  ++inputs.source_identity;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 4u);
  inputs.raster_scale = 2.0f;
  EXPECT_TRUE(submit().dynamic_submitted);
  ASSERT_FALSE(target.dynamic_frame.lines.empty());
  EXPECT_FLOAT_EQ(target.dynamic_frame.lines.front().width_pixels,
                   2.0f * kOverlaySelectionLassoLineWidthPixels);
  EXPECT_EQ(total_builds, 5u);

  marquee.active = false;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.dynamic_frame.lines.empty());
  EXPECT_TRUE(target.dynamic_frame.triangles.empty());
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 6u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hash_count, 0u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hashed_record_count, 0u);
}

} // namespace
} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/primitive_creation_interaction.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/viewport_bounds_resize_overlay_builder.hpp"
#include "render/overlay/viewport_primitive_creation_overlay_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {
namespace {

constexpr std::size_t kDiscTriangles = 96u;

ViewportPrimitiveCreationState primitive_state() {
  return {.object_id = 2,
          .local_min = {-2.0f, -3.0f, -1.0f},
          .local_max = {2.0f, 3.0f, 1.0f},
          .world_origin = {1.0f, 0.5f, -1.0f},
          .world_x = {0.0f, 0.0f, -1.5f},
          .world_y = {0.0f, 0.75f, 0.0f},
          .world_z = {2.0f, 0.0f, 0.0f},
          .revision = 1u};
}

ViewportCameraController bounds_camera() {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(15.0f);
  return camera;
}

std::size_t color_count(const ViewportOverlayFrame &frame, std::uint32_t color) {
  return static_cast<std::size_t>(std::count_if(
      frame.triangles.begin(), frame.triangles.end(),
      [color](const auto &triangle) { return triangle.color == color; }));
}

QVec2 screen_point(const OverlayCameraProjector &projector, QVec3 world) {
  const auto projected = projector.project(projector.view_point_for(world));
  return {projected.pixel.x, projector.camera().viewport_size.y - projected.pixel.y};
}

class BoundsOverlayTarget final : public ViewportOverlaySubmissionTarget {
public:
  ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return {true, true};
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
                                 ViewportOverlayFrame,
                                 ViewportOverlayCamera) override {}
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
                              ViewportOverlayFrame frame,
                              ViewportOverlayCamera) override {
    ++submissions;
    dynamic_frame = std::move(frame);
  }
  std::uint64_t submissions = 0;
  ViewportOverlayFrame dynamic_frame;
};

class CountingBoundsSource final : public ViewportOverlayDynamicToolFrameProvider {
public:
  explicit CountingBoundsSource(ViewportToolOverlaySourceInputs inputs) : source(inputs) {}
  ViewportOverlayFrame build_dynamic_tool_overlay_frame() const override {
    ++builds;
    return source.build_dynamic_tool_overlay_frame();
  }
  ViewportToolOverlaySource source;
  mutable std::uint64_t builds = 0;
};

TEST(PrimitiveCubeOverlayTests, SixAxisDiscsBoundsAndGuidesUseExistingOnTopPolicy) {
  auto camera = bounds_camera();
  PrimitiveCreationInteraction interaction;
  const auto &controls = interaction.project(primitive_state(), camera, 1.0f);
  const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  ViewportOverlayFrame frame;
  ViewportPrimitiveCreationOverlayBuilder::append(frame, overlay_camera, controls);
  ASSERT_EQ(frame.triangles.size(), 6u * kDiscTriangles);
  EXPECT_GE(frame.lines.size(), 12u);
  EXPECT_EQ(color_count(frame, kOverlayToolAxisXColor), 64u);
  EXPECT_EQ(color_count(frame, kOverlayToolAxisYColor), 64u);
  EXPECT_EQ(color_count(frame, kOverlayToolAxisZColor), 64u);
  EXPECT_TRUE(frame.points.empty());
  EXPECT_TRUE(frame.icons.empty());
  for (const auto &triangle : frame.triangles) {
    EXPECT_EQ(triangle.layer, ViewportOverlayLayer::ToolHandle);
    EXPECT_EQ(triangle.source, ViewportOverlaySource::Tool);
    EXPECT_EQ(triangle.style_role, ViewportOverlayStyleRole::ExplicitColor);
  }
  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, overlay_camera, ViewportOverlayLayer::ToolHandle);
  ASSERT_EQ(plan.face_fill_batches.size(), 1u);
  EXPECT_EQ(plan.face_fill_batches.front().mesh.indices.size(), frame.triangles.size() * 3u);
  EXPECT_FALSE(plan.face_fill_batches.front().state.depth_culling);
  EXPECT_FALSE(plan.face_fill_batches.front().state.depth_write);
  EXPECT_EQ(plan.face_fill_batches.front().state.depth_func, OverlayDepthFunc::Always);
  EXPECT_EQ(plan.face_fill_batches.front().state.culling_mode, OverlayCullingMode::None);
}

TEST(PrimitiveCubeOverlayTests, EveryDiscMatchesPublishedPickPixelsAcrossViewsAndScaleDown) {
  auto camera = bounds_camera();
  const auto state = primitive_state();
  for (int view = 0; view < 4; ++view) {
    camera = bounds_camera();
    if (view > 0) camera.view_axis(static_cast<ViewportViewAxis>(view - 1), 1);
    for (const float scale : {1.0f, 2.0f, 1.0f}) {
      PrimitiveCreationInteraction interaction;
      const auto &controls = interaction.project(state, camera, scale);
      const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
      const OverlayCameraProjector projector(overlay_camera);
      for (int index = 0; index < 6; ++index) {
        SCOPED_TRACE(::testing::Message() << view << ";" << scale << ";" << index);
        const auto handle = controls.bounds.handles[index];
        ASSERT_TRUE(handle.visible);
        EXPECT_EQ(interaction.hit_test(handle.screen_position), index);
        auto bounds = controls.bounds;
        for (int other = 0; other < 6; ++other) bounds.handles[other].visible = other == index;
        ViewportOverlayFrame frame;
        ViewportBoundsResizeOverlayBuilder::append(frame, overlay_camera, bounds, ViewportSettings::defaults());
        ASSERT_EQ(frame.triangles.size(), kDiscTriangles);
        QVec2 minimum{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        QVec2 maximum{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
        for (const auto &triangle : frame.triangles) {
          for (const auto vertex : {triangle.a, triangle.b, triangle.c}) {
            const auto screen = screen_point(projector, vertex);
            minimum.x = std::min(minimum.x, screen.x);
            minimum.y = std::min(minimum.y, screen.y);
            maximum.x = std::max(maximum.x, screen.x);
            maximum.y = std::max(maximum.y, screen.y);
            const auto delta = screen - handle.screen_position;
            EXPECT_LE(std::hypot(delta.x, delta.y), handle.radius_pixels + 0.01f);
            EXPECT_NEAR(-projector.view_point_for(vertex).z, handle.depth, 0.0001f);
          }
        }
        EXPECT_NEAR(minimum.x, handle.screen_position.x - handle.radius_pixels, 0.01f);
        EXPECT_NEAR(minimum.y, handle.screen_position.y - handle.radius_pixels, 0.01f);
        EXPECT_NEAR(maximum.x, handle.screen_position.x + handle.radius_pixels, 0.01f);
        EXPECT_NEAR(maximum.y, handle.screen_position.y + handle.radius_pixels, 0.01f);
      }
    }
  }
}

TEST(PrimitiveCubeOverlayTests, BoundsEdgesRetainAffineCornersAndGuidesUseAxisColors) {
  auto camera = bounds_camera();
  PrimitiveCreationInteraction interaction;
  const auto &controls = interaction.project(primitive_state(), camera, 2.0f);
  const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  ViewportOverlayFrame frame;
  ViewportPrimitiveCreationOverlayBuilder::append(frame, overlay_camera, controls);
  ASSERT_GE(frame.lines.size(), 12u);
  std::size_t edge = 0;
  for (int corner = 0; corner < 8; ++corner) {
    for (int axis = 0; axis < 3; ++axis) {
      if ((corner & (1 << axis)) != 0) continue;
      const auto &line = frame.lines[edge++];
      EXPECT_FLOAT_EQ(length(line.start - controls.bounds.corners[corner]), 0.0f);
      EXPECT_FLOAT_EQ(length(line.end - controls.bounds.corners[corner | (1 << axis)]), 0.0f);
      EXPECT_EQ(line.color, kOverlayToolAxisMixedColor);
    }
  }
  const OverlayCameraProjector projector(overlay_camera);
  for (std::size_t index = 12; index < frame.lines.size(); ++index) {
    const auto &line = frame.lines[index];
    EXPECT_TRUE(line.color == kOverlayToolAxisXColor || line.color == kOverlayToolAxisYColor ||
                line.color == kOverlayToolAxisZColor);
    const auto delta = screen_point(projector, line.end) - screen_point(projector, line.start);
    EXPECT_LE(std::hypot(delta.x, delta.y), 16.01f);
  }
}

TEST(PrimitiveCubeOverlayTests, PrimitiveAndMeshBoundsShareExactlyTheSameDrawGeometry) {
  auto camera = bounds_camera();
  PrimitiveCreationInteraction interaction;
  const auto &controls = interaction.project(primitive_state(), camera, 1.0f);
  const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  ViewportOverlayFrame primitive;
  ViewportOverlayFrame common;
  ViewportPrimitiveCreationOverlayBuilder::append(primitive, overlay_camera, controls);
  ViewportBoundsResizeOverlayBuilder::append(common, overlay_camera, controls.bounds, ViewportSettings::defaults());
  ASSERT_EQ(primitive.triangles.size(), common.triangles.size());
  ASSERT_EQ(primitive.lines.size(), common.lines.size());
  for (std::size_t index = 0; index < common.triangles.size(); ++index) {
    EXPECT_FLOAT_EQ(length(primitive.triangles[index].a - common.triangles[index].a), 0.0f);
    EXPECT_FLOAT_EQ(length(primitive.triangles[index].b - common.triangles[index].b), 0.0f);
    EXPECT_FLOAT_EQ(length(primitive.triangles[index].c - common.triangles[index].c), 0.0f);
    EXPECT_EQ(primitive.triangles[index].color, common.triangles[index].color);
  }
}

TEST(PrimitiveCubeOverlayTests, HoverAndCaptureUsePublishedSemanticAxisHighlights) {
  auto camera = bounds_camera();
  PrimitiveCreationInteraction interaction;
  const auto state = primitive_state();
  interaction.project(state, camera, 1.0f);
  const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  interaction.hover(2);
  interaction.begin(4, state, camera, interaction.frame().controls[4].screen_position);
  ViewportOverlayFrame frame;
  ViewportPrimitiveCreationOverlayBuilder::append(frame, overlay_camera, interaction.frame());
  EXPECT_EQ(color_count(frame, kOverlayToolAxisYHighlightColor), 32u);
  EXPECT_EQ(color_count(frame, kOverlayToolAxisZHighlightColor), 32u);
  EXPECT_EQ(color_count(frame, kOverlayToolAxisXHighlightColor), 0u);
  EXPECT_EQ(frame.triangles.back().color, kOverlayPrimitiveCubeFaceOutlineColor);
}

TEST(PrimitiveCubeOverlayTests, HiddenOrInvalidBoundsTargetsAppendNoHandleTriangles) {
  auto camera = bounds_camera();
  PrimitiveCreationInteraction interaction;
  interaction.project(primitive_state(), camera, 1.0f);
  auto bounds = interaction.frame().bounds;
  const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  bounds.visible = false;
  ViewportOverlayFrame hidden;
  ViewportBoundsResizeOverlayBuilder::append(hidden, overlay_camera, bounds, ViewportSettings::defaults());
  EXPECT_TRUE(hidden.lines.empty());
  EXPECT_TRUE(hidden.triangles.empty());
  bounds.visible = true;
  for (auto &handle : bounds.handles) handle.radius_pixels = std::numeric_limits<float>::quiet_NaN();
  ViewportOverlayFrame invalid;
  ViewportBoundsResizeOverlayBuilder::append(invalid, overlay_camera, bounds, ViewportSettings::defaults());
  EXPECT_TRUE(invalid.triangles.empty());
}

TEST(PrimitiveCubeOverlayTests, SubmittedBoundsReuseIdleFramesAndRefreshHoverGeometryAndRetirement) {
  auto camera = bounds_camera();
  auto state = primitive_state();
  PrimitiveCreationInteraction interaction;
  ViewportOverlaySubmissionCoordinator coordinator;
  BoundsOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  std::uint64_t builds = 0;
  bool visible = true;
  const auto submit = [&] {
    const auto &controls = interaction.project(visible ? std::optional{state} : std::nullopt, camera, 1.0f);
    const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
    const CountingBoundsSource provider({
        .source_identity = 44u,
        .overlay_camera = &overlay_camera,
        .primitive_creation_controls = &controls,
        .interaction_pane = true,
    });
    const auto result = coordinator.submit(
        {.layer_mask = 1u,
         .camera = overlay_camera,
         .dynamic_tool_source = provider.source.source_stamp(),
         .dynamic_tool_frame_provider = &provider}, settings, target);
    builds += provider.builds;
    return result;
  };
  ASSERT_TRUE(submit().dynamic_submitted);
  const auto projections = interaction.projection_count();
  for (int iteration = 0; iteration < 120; ++iteration) EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(builds, 1u);
  EXPECT_EQ(interaction.projection_count(), projections);
  interaction.hover(0);
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(color_count(target.dynamic_frame, kOverlayToolAxisXHighlightColor), 32u);
  EXPECT_EQ(interaction.projection_count(), projections);
  state.local_max.x += 1.0f;
  ++state.revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_FLOAT_EQ(length(target.dynamic_frame.lines.front().end -
      interaction.frame().bounds.corners[1]), 0.0f);
  const auto accepted_builds = builds;
  for (int iteration = 0; iteration < 120; ++iteration) EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(builds, accepted_builds);
  visible = false;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.dynamic_frame.lines.empty());
  EXPECT_TRUE(target.dynamic_frame.triangles.empty());
}

} // namespace
} // namespace quader

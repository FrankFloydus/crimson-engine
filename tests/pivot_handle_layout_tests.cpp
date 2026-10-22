////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/pivot/pivot_types.hpp"
#include "editor/tools/bounds_resize_interaction.hpp"
#include "editor/tools/pivot_handle_layout.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace quader {
namespace {

constexpr std::uint32_t kIconColor = rgba8(255, 255, 89, 255);
constexpr std::uint32_t kGapColor = rgba8(12, 12, 14, 255);
constexpr std::size_t kIconTriangles = 160u;

ViewportCameraController pivot_camera(float distance = 10.0f) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(distance);
  return camera;
}

PivotHandleState pivot_handle(bool persistent = true) {
  return {.visible = true, .editing = true, .persistent = persistent};
}

float area(QVec2 a, QVec2 b, QVec2 c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

std::optional<std::uint32_t> painted_color(
    const ViewportOverlayFrame &frame, const ViewportCameraController &camera,
    QVec2 point) {
  std::optional<std::uint32_t> result;
  for (const auto &triangle : frame.triangles) {
    const auto a = camera.project_world_point(triangle.a).position;
    const auto b = camera.project_world_point(triangle.b).position;
    const auto c = camera.project_world_point(triangle.c).position;
    const float ab = area(a, b, point);
    const float bc = area(b, c, point);
    const float ca = area(c, a, point);
    if ((ab >= -0.00001f && bc >= -0.00001f && ca >= -0.00001f) ||
        (ab <= 0.00001f && bc <= 0.00001f && ca <= 0.00001f)) {
      result = triangle.color;
    }
  }
  return result;
}

/// Records accepted dynamic frames without replacing the submission coordinator.
class PivotOverlayTarget final : public ViewportOverlaySubmissionTarget {
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
    submitted = std::move(frame);
  }
  std::uint64_t submissions = 0;
  ViewportOverlayFrame submitted;
};

/// Counts production source composition to verify unchanged-frame retention.
class CountingPivotSource final : public ViewportOverlayDynamicToolFrameProvider {
public:
  explicit CountingPivotSource(ViewportToolOverlaySourceInputs inputs) : source(inputs) {}
  ViewportOverlayFrame build_dynamic_tool_overlay_frame() const override {
    ++builds;
    return source.build_dynamic_tool_overlay_frame();
  }
  ViewportToolOverlaySource source;
  mutable std::uint64_t builds = 0;
};

TEST(PivotHandleLayoutTests, BillboardSizeAndPickRadiusAgreeAcrossViewsZoomAndScaleDown) {
  for (const float distance : {3.0f, 10.0f, 80.0f}) {
    for (int view = 0; view < 4; ++view) {
      auto camera = pivot_camera(distance);
      if (view > 0) camera.view_axis(static_cast<ViewportViewAxis>(view - 1), 1);
      for (const float scale : {1.0f, 2.0f, 1.0f}) {
        const auto handle = pivot_handle(false);
        const auto layout = build_pivot_handle_layout(camera, handle, scale);
        ASSERT_TRUE(layout.valid);
        EXPECT_FLOAT_EQ(layout.icon_radius_pixels, 10.0f * scale);
        const auto rim = camera.project_world_point(layout.center + layout.camera_right *
            (layout.icon_radius_pixels * layout.world_units_per_pixel));
        EXPECT_NEAR(std::hypot(rim.position.x - layout.center_screen.x,
                               rim.position.y - layout.center_screen.y), 10.0f * scale, 0.001f);
        EXPECT_EQ(hit_pivot_handle(layout, layout.center_screen), PivotHandlePart::Center);
        EXPECT_EQ(hit_pivot_handle(layout, layout.center_screen + QVec2{9.9f * scale, 0}),
                  PivotHandlePart::Center);
        EXPECT_EQ(hit_pivot_handle(layout, layout.center_screen + QVec2{10.1f * scale, 0}),
                  PivotHandlePart::None);
        ViewportOverlayFrame frame;
        ViewportToolOverlayProjection::append_pivot_handle(frame, camera, handle, scale);
        ASSERT_EQ(frame.triangles.size(), kIconTriangles);
        for (const auto &triangle : frame.triangles) {
          for (const auto vertex : {triangle.a, triangle.b, triangle.c}) {
            EXPECT_NEAR(dot(vertex - layout.center,
                normalize(camera.pose().target - camera.pose().eye)), 0.0f, 0.0001f);
          }
        }
      }
    }
  }
}

TEST(PivotHandleLayoutTests, IconContainsFilledCenterDarkGapAndThinOuterRing) {
  auto camera = pivot_camera();
  const auto handle = pivot_handle(false);
  const auto layout = build_pivot_handle_layout(camera, handle);
  ViewportOverlayFrame frame;
  ViewportToolOverlayProjection::append_pivot_handle(frame, camera, handle);
  EXPECT_EQ(painted_color(frame, camera, layout.center_screen), kIconColor);
  EXPECT_EQ(painted_color(frame, camera, layout.center_screen + QVec2{3.0f, 1.0f}), kIconColor);
  EXPECT_EQ(painted_color(frame, camera, layout.center_screen + QVec2{6.0f, 2.0f}), kGapColor);
  EXPECT_EQ(painted_color(frame, camera, layout.center_screen + QVec2{9.0f, 2.0f}), kIconColor);
  EXPECT_FALSE(painted_color(frame, camera, layout.center_screen + QVec2{11.0f, 0.0f}));
  EXPECT_TRUE(frame.lines.empty());
  EXPECT_TRUE(frame.points.empty());
  const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  const auto plan = OverlayRenderableBuilder::build_layer(frame, overlay_camera, ViewportOverlayLayer::ToolHandle);
  ASSERT_EQ(plan.face_fill_batches.size(), 1u);
  EXPECT_EQ(plan.face_fill_batches.front().state.depth_func, OverlayDepthFunc::Always);
  EXPECT_FALSE(plan.face_fill_batches.front().state.depth_write);
  EXPECT_EQ(plan.face_fill_batches.front().state.culling_mode, OverlayCullingMode::None);
}

TEST(PivotHandleLayoutTests, PersistentAxesSharePickingAndSemanticOrientationWhileQuickPivotHasOnlyIcon) {
  auto camera = pivot_camera();
  PivotHandleLayoutOptions options;
  options.world_x = {0.0f, 0.0f, 2.0f};
  options.world_y = {0.0f, 3.0f, 0.0f};
  options.world_z = {-4.0f, 0.0f, 0.0f};
  const auto handle = pivot_handle();
  const auto layout = build_pivot_handle_layout(camera, handle, 1.0f, options);
  ASSERT_TRUE(layout.valid);
  EXPECT_FLOAT_EQ(length(layout.axes[0].world_direction - QVec3{0, 0, 1}), 0.0f);
  std::size_t visible = 0;
  for (const auto &axis : layout.axes) {
    if (!axis.visible) continue;
    ++visible;
    EXPECT_EQ(hit_pivot_handle(layout, axis.screen_tip), axis.part);
  }
  EXPECT_GT(visible, 0u);
  ViewportOverlayFrame frame;
  ViewportToolOverlayProjection::append_pivot_handle(frame, camera, handle, 1.0f, options,
      PivotHandlePart::X, PivotHandlePart::Y);
  EXPECT_EQ(frame.triangles.size(), kIconTriangles + visible * 3u);
  EXPECT_TRUE(frame.lines.empty());
  EXPECT_EQ(hit_pivot_handle(layout, layout.center_screen), PivotHandlePart::Center);
  const auto quick = build_pivot_handle_layout(camera, pivot_handle(false), 1.0f, options);
  for (const auto &axis : quick.axes) EXPECT_FALSE(axis.visible);
}

TEST(PivotHandleLayoutTests, BoundsForegroundMatchesFreshHitPriorityOverPivotShaftAndTip) {
  for (const float scale : {1.0f, 2.0f, 1.0f}) {
    auto camera = pivot_camera();
    camera.view_axis(ViewportViewAxis::Z, 1);
    const auto settings = ViewportSettings::defaults();
    const ViewportBoundsResizeState state{
        .local_min = {-1.0f, -1.0f, -1.0f},
        .local_max = {1.0f, 1.0f, 1.0f}};
    BoundsResizeInteraction bounds_interaction;
    const auto &bounds = bounds_interaction.project(state, camera, scale);
    const auto target = bounds.handles[2].screen_position;
    ASSERT_EQ(bounds_interaction.hit_test(target), 2);
    const auto reference = build_pivot_handle_layout(camera, pivot_handle(), scale);
    ASSERT_TRUE(reference.valid);
    for (const float axis_distance : {24.0f, 44.0f}) {
      auto pivot = pivot_handle();
      const auto delta = target - reference.center_screen - QVec2{axis_distance * scale, 0};
      pivot.world_position = reference.center +
          (reference.camera_right * delta.x - reference.camera_up * delta.y) *
          reference.world_units_per_pixel;
      const auto layout = build_pivot_handle_layout(camera, pivot, scale);
      ASSERT_EQ(hit_pivot_handle(layout, target), PivotHandlePart::X);
      const auto color_sample = axis_distance > 40.0f
          ? target - QVec2{2.0f * scale, 0} : target;
      ViewportOverlayFrame pivot_frame;
      ViewportToolOverlayProjection::append_pivot_handle(pivot_frame, camera, pivot, scale);
      EXPECT_EQ(painted_color(pivot_frame, camera, color_sample), kOverlayToolAxisXColor);
      EXPECT_TRUE(pivot_frame.lines.empty());
      const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
      const ViewportToolOverlaySource source({
          .raster_scale = scale,
          .overlay_camera = &overlay_camera,
          .viewport_camera = &camera,
          .mesh_bounds_resize = &bounds,
          .viewport_settings = &settings,
          .pivot_handle = &pivot,
          .gizmos_visible = true,
          .pivot_handle_editing = true});
      const auto frame = source.build_dynamic_tool_overlay_frame();
      EXPECT_EQ(painted_color(frame, camera, color_sample), kOverlayToolAxisYColor);
      const auto plan = OverlayRenderableBuilder::build_layer(
          frame, overlay_camera, ViewportOverlayLayer::ToolHandle);
      EXPECT_TRUE(plan.line_batches.empty());
      EXPECT_EQ(plan.face_fill_batches.size(), 1u);
      EXPECT_EQ(bounds_interaction.hit_test(target), 2);
    }
  }
}

TEST(PivotHandleLayoutTests, CoincidentProjectedAxesPickTheControlPaintedInFront) {
  auto camera = pivot_camera();
  camera.view_axis(ViewportViewAxis::Z, 1);
  camera.set_view_direction({1.0f, -1.0f, 0.0f});
  const auto handle = pivot_handle();
  for (const float scale : {1.0f, 2.0f}) {
    SCOPED_TRACE(scale);
    const auto layout = build_pivot_handle_layout(camera, handle, scale);
    ASSERT_TRUE(layout.valid);
    const auto &x = layout.axes[0];
    const auto &y = layout.axes[1];
    ASSERT_TRUE(x.visible && y.visible);
    EXPECT_NEAR(x.screen_start.x, y.screen_start.x, 0.001f);
    EXPECT_NEAR(x.screen_start.y, y.screen_start.y, 0.001f);
    EXPECT_NEAR(x.screen_tip.x, y.screen_tip.x, 0.001f);
    EXPECT_NEAR(x.screen_tip.y, y.screen_tip.y, 0.001f);
    ViewportOverlayFrame frame;
    ViewportToolOverlayProjection::append_pivot_handle(frame, camera, handle, scale);
    const auto direction = x.screen_tip - layout.center_screen;
    const float span = std::hypot(direction.x, direction.y);
    ASSERT_GT(span, 0.0f);
    for (const float distance : {24.0f, 42.0f}) {
      const auto point = layout.center_screen + direction * (distance * scale / span);
      EXPECT_EQ(painted_color(frame, camera, point), kOverlayToolAxisYColor);
      EXPECT_EQ(hit_pivot_handle(layout, point), PivotHandlePart::Y);
    }
    EXPECT_EQ(hit_pivot_handle(layout, x.screen_tip), PivotHandlePart::Y);
    EXPECT_EQ(hit_pivot_handle(layout, y.screen_tip), PivotHandlePart::Y);
    EXPECT_EQ(hit_pivot_handle(layout, layout.center_screen), PivotHandlePart::Center);
  }
}

TEST(PivotHandleLayoutTests, EndOnAxesRetireWithoutMaskingTheCenter) {
  for (int axis = 0; axis < 3; ++axis) {
    auto camera = pivot_camera();
    camera.view_axis(static_cast<ViewportViewAxis>(axis), 1);
    const auto layout = build_pivot_handle_layout(camera, pivot_handle());
    ASSERT_TRUE(layout.valid);
    EXPECT_FALSE(layout.axes[axis].visible);
    EXPECT_EQ(hit_pivot_handle(layout, layout.center_screen), PivotHandlePart::Center);
    for (const auto &target : layout.axes) {
      if (target.visible) EXPECT_EQ(hit_pivot_handle(layout, target.screen_tip), target.part);
    }
  }
}

TEST(PivotHandleLayoutTests, IconPreferencesChangeRasterAndHitGeometryWithoutResizingAxes) {
  auto camera = pivot_camera();
  const auto handle = pivot_handle();
  const auto before = build_pivot_handle_layout(camera, handle);
  PivotHandleLayoutOptions options;
  options.icon_size_pixels = 48.0f;
  const auto after = build_pivot_handle_layout(camera, handle, 1.0f, options);
  EXPECT_FLOAT_EQ(after.icon_radius_pixels, 24.0f);
  EXPECT_EQ(hit_pivot_handle(after, after.center_screen + QVec2{20.0f, 0}), PivotHandlePart::Center);
  EXPECT_NE(hit_pivot_handle(before, before.center_screen + QVec2{20.0f, 0}), PivotHandlePart::Center);
  for (int axis = 0; axis < 3; ++axis) {
    EXPECT_NEAR(length(before.axes[axis].world_tip - after.axes[axis].world_tip), 0.0f, 0.000001f);
  }
  options.icon_size_pixels = 64.0f;
  const auto largest = build_pivot_handle_layout(camera, handle, 1.0f, options);
  for (std::size_t axis = 0u; axis < largest.axes.size(); ++axis) {
    EXPECT_EQ(largest.axes[axis].visible, before.axes[axis].visible);
    if (!largest.axes[axis].visible) continue;
    const auto delta = largest.axes[axis].screen_tip - largest.center_screen;
    EXPECT_NEAR(std::hypot(delta.x, delta.y), 44.0f, 0.01f);
  }
  auto settings = ViewportSettings::defaults();
  settings.set_float_value(ViewportFloatSettingId::PivotIconSize, 48.0f);
  settings.set_color(ViewportColorSettingId::PivotIcon, {0.0f, 0.25f, 1.0f, 1.0f});
  ViewportOverlayFrame frame;
  ViewportToolOverlayProjection::append_pivot_handle(frame, camera, handle, 1.0f, {},
      PivotHandlePart::None, PivotHandlePart::None, &settings);
  EXPECT_EQ(painted_color(frame, camera, after.center_screen), rgba8(0, 64, 255));
  EXPECT_EQ(painted_color(frame, camera, after.center_screen + QVec2{22.0f, 0}), rgba8(0, 64, 255));
}

TEST(PivotHandleLayoutTests, HiddenInvalidAndOffCameraHandlesCannotBePicked) {
  auto camera = pivot_camera();
  auto handle = pivot_handle();
  handle.visible = false;
  EXPECT_FALSE(build_pivot_handle_layout(camera, handle).valid);
  handle.visible = true;
  handle.world_position.x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(build_pivot_handle_layout(camera, handle).valid);
  handle.world_position = camera.pose().eye * 2.0f;
  EXPECT_FALSE(build_pivot_handle_layout(camera, handle).valid);
  handle.world_position = {};
  PivotHandleLayoutOptions options;
  options.icon_size_pixels = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FLOAT_EQ(build_pivot_handle_layout(camera, handle, 1.0f, options).icon_radius_pixels, 10.0f);
  options.icon_size_pixels = 1000.0f;
  EXPECT_FLOAT_EQ(build_pivot_handle_layout(camera, handle, 1.0f, options).icon_radius_pixels, 32.0f);
  const auto layout = build_pivot_handle_layout(camera, handle);
  EXPECT_EQ(hit_pivot_handle(layout, {std::numeric_limits<float>::infinity(), 0}), PivotHandlePart::None);
}

TEST(PivotHandleLayoutTests, SourceStampsGateIdleWorkAndIncludePreferencesBasisHoverCaptureAndCamera) {
  auto camera = pivot_camera();
  auto handle = pivot_handle();
  auto settings = ViewportSettings::defaults();
  auto options = PivotHandleLayoutOptions{};
  auto hovered = PivotHandlePart::None;
  auto active = PivotHandlePart::None;
  bool interaction_pane = true;
  bool gizmos_visible = true;
  ViewportOverlaySubmissionCoordinator coordinator;
  PivotOverlayTarget target;
  std::uint64_t builds = 0;
  const auto submit = [&] {
    const auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
    const CountingPivotSource provider({
        .source_identity = 35u,
        .overlay_camera = &overlay_camera,
        .viewport_camera = &camera,
        .viewport_settings = &settings,
        .pivot_handle = &handle,
        .pivot_layout_options = options,
        .pivot_hovered_part = hovered,
        .pivot_active_part = active,
        .interaction_pane = interaction_pane,
        .gizmos_visible = gizmos_visible,
    });
    const auto result = coordinator.submit({.layer_mask = 1u, .camera = overlay_camera,
        .dynamic_tool_source = provider.source.source_stamp(),
        .dynamic_tool_frame_provider = &provider}, settings, target);
    builds += provider.builds;
    return result;
  };
  EXPECT_TRUE(submit().dynamic_submitted);
  for (int iteration = 0; iteration < 100; ++iteration) EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(builds, 1u);
  hovered = PivotHandlePart::X;
  EXPECT_TRUE(submit().dynamic_submitted);
  active = PivotHandlePart::Y;
  EXPECT_TRUE(submit().dynamic_submitted);
  interaction_pane = false;
  EXPECT_TRUE(submit().dynamic_submitted);
  const auto passive_builds = builds;
  hovered = PivotHandlePart::Z;
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(builds, passive_builds);
  options.world_x = {0.0f, 0.0f, 1.0f};
  EXPECT_TRUE(submit().dynamic_submitted);
  settings.set_float_value(ViewportFloatSettingId::PivotIconSize, 32.0f);
  EXPECT_TRUE(submit().dynamic_submitted);
  settings.set_color(ViewportColorSettingId::PivotIcon, {0.0f, 1.0f, 0.0f, 1.0f});
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(painted_color(target.submitted, camera,
      camera.project_world_point(handle.world_position).position), rgba8(0, 255, 0));
  camera.focus_origin(20.0f);
  EXPECT_TRUE(submit().dynamic_submitted);
  handle.visible = false;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.submitted.triangles.empty());
  EXPECT_TRUE(target.submitted.lines.empty());
  handle.visible = true;
  EXPECT_TRUE(submit().dynamic_submitted);
  gizmos_visible = false;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.submitted.triangles.empty());
  EXPECT_TRUE(target.submitted.lines.empty());
  const auto hidden_builds = builds;
  handle.world_position = {1.0f, 2.0f, 3.0f};
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(builds, hidden_builds);
  // Global style changes resubmit the retained empty frame. Camera changes
  // can use the camera-only path while the hidden source retains its geometry.
  settings.set_color(ViewportColorSettingId::PivotIcon, {1.0f, 0.0f, 0.0f, 1.0f});
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(builds, hidden_builds);
  camera.focus_origin(10.0f);
  const auto camera_only = submit();
  EXPECT_FALSE(camera_only.dynamic_submitted);
  EXPECT_TRUE(camera_only.camera_only);
  EXPECT_EQ(builds, hidden_builds);
}

} // namespace
} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/extrude_handle_layout.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace quader {
namespace {

constexpr std::array kExtrusionModes{
    modeling::FaceExtrudeMode::Region,
    modeling::FaceExtrudeMode::AlongNormals,
    modeling::FaceExtrudeMode::Individual};
constexpr std::uint32_t kDiskColor = rgba8(255, 255, 132, 255);
constexpr std::uint32_t kActiveDiskColor = rgba8(255, 255, 255, 255);

float pixel_length(QVec2 value) {
  return std::hypot(value.x, value.y);
}

ViewportCameraController extrusion_camera(float distance = 6.0F) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0F, 600.0F);
  camera.focus_origin(distance);
  camera.set_view_direction({0.0F, 0.0F, -1.0F});
  return camera;
}

ExtrudeHandleState extrusion_handle(modeling::FaceExtrudeMode mode) {
  ExtrudeHandleState handle;
  handle.active = true;
  handle.mode = mode;
  handle.world_direction = {1.0F, 0.0F, 0.0F};
  return handle;
}

void expect_vector_near(QVec3 actual, QVec3 expected,
                        float tolerance = 0.0001F) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

ViewportOverlayFrame extrusion_overlay(const ViewportCameraController &camera,
                                       const ExtrudeHandleState &handle,
                                       float scale = 1.0F) {
  ViewportOverlayFrame frame;
  ViewportToolOverlayProjection::append_extrusion_handle(frame, camera, handle,
                                                        scale);
  return frame;
}

void expect_overlay_color(const ViewportOverlayFrame &frame,
                          std::uint32_t expected) {
  ASSERT_FALSE(frame.triangles.empty());
  ASSERT_EQ(frame.lines.size(), 1u);
  for (const auto &triangle : frame.triangles) {
    EXPECT_EQ(triangle.color, expected);
  }
  EXPECT_EQ(frame.lines.front().color, expected);
}

TEST(ExtrudeHandleLayoutTests, AllModesUseOneCommonWorldRadiusAndNormalOffset) {
  const auto camera = extrusion_camera();
  for (const auto mode : kExtrusionModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    const auto handle = extrusion_handle(mode);
    const auto layout = build_extrude_handle_layout(camera, handle);
    ASSERT_TRUE(layout.valid);
    EXPECT_EQ(layout.mode, mode);
    EXPECT_NEAR(layout.button_world_radius,
                16.0F * layout.world_units_per_pixel, 0.0001F);
    EXPECT_NEAR(layout.button_radius_pixels, 16.0F, 0.001F);
    expect_vector_near(layout.pivot, handle.world_position);
    expect_vector_near(layout.button_center - layout.pivot,
                       handle.world_direction * 12.0F * layout.button_world_radius);
    EXPECT_NEAR(pixel_length(layout.button_center_screen - layout.pivot_screen),
                192.0F, 0.02F);
    EXPECT_EQ(hit_extrude_handle(layout, layout.button_center_screen),
              ExtrudeHandlePart::NewExtrusion);
  }
}

TEST(ExtrudeHandleLayoutTests, OnlyTheDiskInterceptsInputAndTheStemIsDecorative) {
  const auto camera = extrusion_camera();
  for (const auto mode : kExtrusionModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    const auto layout =
        build_extrude_handle_layout(camera, extrusion_handle(mode));
    ASSERT_TRUE(layout.valid);
    const float radius = layout.button_radius_pixels;
    for (const QVec2 direction :
         {QVec2{1.0F, 0.0F}, QVec2{-1.0F, 0.0F}, QVec2{0.0F, 1.0F},
          QVec2{0.0F, -1.0F}}) {
      EXPECT_EQ(hit_extrude_handle(layout, layout.button_center_screen +
                                   direction * radius * 0.9F),
                ExtrudeHandlePart::NewExtrusion);
      EXPECT_EQ(hit_extrude_handle(layout, layout.button_center_screen +
                                   direction * (radius + 2.0F)),
                ExtrudeHandlePart::None);
    }
    EXPECT_EQ(hit_extrude_handle(layout, layout.button_center_screen +
                                 QVec2{radius * 0.8F, radius * 0.8F}),
              ExtrudeHandlePart::None);
    EXPECT_EQ(hit_extrude_handle(layout, layout.pivot_screen),
              ExtrudeHandlePart::None);
    EXPECT_EQ(hit_extrude_handle(layout, (layout.pivot_screen +
                                         layout.button_center_screen) * 0.5F),
              ExtrudeHandlePart::None);
  }
}

TEST(ExtrudeHandleLayoutTests, PerspectiveProjectsTheActualNormalOffsetAndWorldRadius) {
  const auto camera = extrusion_camera();
  const auto basis = ViewportCameraAdapter::basis(camera);
  for (const float normal_z : {-1.0F, 1.0F}) {
    SCOPED_TRACE(normal_z);
    auto handle = extrusion_handle(modeling::FaceExtrudeMode::Region);
    handle.world_direction = normalize(QVec3{1.0F, 0.0F, normal_z});
    const auto layout = build_extrude_handle_layout(camera, handle);
    ASSERT_TRUE(layout.valid);
    expect_vector_near(layout.button_center - layout.pivot,
                       handle.world_direction * 12.0F * layout.button_world_radius);
    const auto projected = camera.project_world_point(layout.button_center);
    ASSERT_TRUE(projected.visible);
    EXPECT_NEAR(pixel_length(projected.position - layout.button_center_screen),
                0.0F, 0.001F);
    const float pivot_depth = dot(layout.pivot - basis.eye, basis.forward);
    const float button_depth = dot(layout.button_center - basis.eye, basis.forward);
    EXPECT_NEAR(layout.button_radius_pixels, 16.0F * pivot_depth / button_depth,
                0.001F);
    if (normal_z > 0.0F) {
      EXPECT_GT(layout.button_radius_pixels, 16.0F);
    } else {
      EXPECT_LT(layout.button_radius_pixels, 16.0F);
    }
    EXPECT_EQ(hit_extrude_handle(layout, layout.button_center_screen),
              ExtrudeHandlePart::NewExtrusion);
  }
}

TEST(ExtrudeHandleLayoutTests, EndOnNormalsKeepTheDiskOverThePivotWithoutFallback) {
  const auto camera = extrusion_camera();
  for (const auto mode : kExtrusionModes) {
    for (const float sign : {-1.0F, 1.0F}) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(sign);
      auto handle = extrusion_handle(mode);
      handle.world_direction = {0.0F, 0.0F, sign};
      const auto layout = build_extrude_handle_layout(camera, handle);
      ASSERT_TRUE(layout.valid);
      expect_vector_near(layout.button_center - layout.pivot,
                         handle.world_direction * 12.0F * layout.button_world_radius);
      EXPECT_NEAR(pixel_length(layout.button_center_screen - layout.pivot_screen),
                  0.0F, 0.001F);
      EXPECT_EQ(hit_extrude_handle(layout, layout.pivot_screen),
                ExtrudeHandlePart::NewExtrusion);
      const auto frame = extrusion_overlay(camera, handle);
      ASSERT_FALSE(frame.triangles.empty());
      for (const auto &line : frame.lines) {
        for (const QVec3 point : {line.start, line.end}) {
          EXPECT_TRUE(std::isfinite(point.x));
          EXPECT_TRUE(std::isfinite(point.y));
          EXPECT_TRUE(std::isfinite(point.z));
        }
      }
    }
  }
}

TEST(ExtrudeHandleLayoutTests, ZoomAndRasterScaleSetTheRadiusAtThePivotDepth) {
  for (const auto mode : kExtrusionModes) {
    for (const float distance : {6.0F, 18.0F}) {
      for (const float scale : {1.0F, 2.0F}) {
        SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(distance);
        SCOPED_TRACE(scale);
        const auto camera = extrusion_camera(distance);
        const auto layout =
            build_extrude_handle_layout(camera, extrusion_handle(mode), scale);
        ASSERT_TRUE(layout.valid);
        EXPECT_NEAR(layout.raster_scale, scale, 0.001F);
        EXPECT_NEAR(layout.button_world_radius,
                    16.0F * scale * layout.world_units_per_pixel, 0.0001F);
        EXPECT_NEAR(layout.button_radius_pixels, 16.0F * scale, 0.002F);
        EXPECT_NEAR(pixel_length(layout.button_center_screen - layout.pivot_screen),
                    192.0F * scale, 0.04F);
        EXPECT_EQ(hit_extrude_handle(layout, layout.button_center_screen),
                  ExtrudeHandlePart::NewExtrusion);
      }
    }
  }
}

TEST(ExtrudeHandleLayoutTests, OrthographicViewRetainsForeshorteningAndDiskSize) {
  auto camera = extrusion_camera();
  camera.view_axis(ViewportViewAxis::Z, 1);
  camera.focus_origin(6.0F);
  auto handle = extrusion_handle(modeling::FaceExtrudeMode::AlongNormals);
  handle.world_direction = normalize(QVec3{1.0F, 0.0F, 1.0F});
  const auto layout = build_extrude_handle_layout(camera, handle);
  ASSERT_TRUE(layout.valid);
  expect_vector_near(layout.button_center - layout.pivot,
                     handle.world_direction * 12.0F * layout.button_world_radius);
  EXPECT_NEAR(layout.button_radius_pixels, 16.0F, 0.001F);
  EXPECT_NEAR(pixel_length(layout.button_center_screen - layout.pivot_screen),
              192.0F / std::sqrt(2.0F), 0.02F);
}

TEST(ExtrudeHandleLayoutTests, AdjustmentStateAddsNoArrowAndDoesNotMoveTheDisk) {
  const auto camera = extrusion_camera();
  for (const auto mode : kExtrusionModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto handle = extrusion_handle(mode);
    const auto initial = build_extrude_handle_layout(camera, handle);
    const auto before = extrusion_overlay(camera, handle);
    handle.adjustment_available = true;
    handle.adjustment_origin = {2.0F, -1.0F, 0.0F};
    handle.adjustment_direction = {0.0F, 1.0F, 0.0F};
    handle.amount_world = -2.15;
    handle.adjustment_hovered = true;
    handle.adjusting = true;
    const auto adjusted = build_extrude_handle_layout(camera, handle);
    const auto after = extrusion_overlay(camera, handle);
    ASSERT_TRUE(adjusted.valid);
    expect_vector_near(adjusted.button_center, initial.button_center);
    EXPECT_FLOAT_EQ(adjusted.button_world_radius, initial.button_world_radius);
    ASSERT_EQ(after.triangles.size(), before.triangles.size());
    ASSERT_EQ(after.lines.size(), 1u);
    expect_overlay_color(after, kDiskColor);
    for (std::size_t index = 0; index < before.triangles.size(); ++index) {
      expect_vector_near(after.triangles[index].a, before.triangles[index].a);
      expect_vector_near(after.triangles[index].b, before.triangles[index].b);
      expect_vector_near(after.triangles[index].c, before.triangles[index].c);
    }
    EXPECT_EQ(hit_extrude_handle(adjusted, adjusted.button_center_screen),
              ExtrudeHandlePart::NewExtrusion);
  }
}

TEST(ExtrudeHandleLayoutTests, InactiveStateAndNonFinitePointersCannotBePicked) {
  const auto camera = extrusion_camera();
  auto handle = extrusion_handle(modeling::FaceExtrudeMode::Region);
  handle.active = false;
  const auto inactive = build_extrude_handle_layout(camera, handle);
  EXPECT_FALSE(inactive.valid);
  EXPECT_EQ(hit_extrude_handle(inactive, {}), ExtrudeHandlePart::None);
  const auto frame = extrusion_overlay(camera, handle);
  EXPECT_TRUE(frame.triangles.empty());
  EXPECT_TRUE(frame.lines.empty());
  EXPECT_TRUE(frame.points.empty());
  handle.active = true;
  const auto active = build_extrude_handle_layout(camera, handle);
  for (const float invalid : {std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    EXPECT_EQ(hit_extrude_handle(active, {invalid, 300.0F}),
              ExtrudeHandlePart::None);
    EXPECT_EQ(hit_extrude_handle(active, {400.0F, invalid}),
              ExtrudeHandlePart::None);
  }
}

TEST(ExtrudeHandleLayoutTests, OverlayIsOneOpaquePaleYellowDiskAndOneThinStem) {
  const auto camera = extrusion_camera();
  for (const auto mode : kExtrusionModes) {
    for (const float scale : {1.0F, 1.5F}) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(scale);
      const auto handle = extrusion_handle(mode);
      const auto layout = build_extrude_handle_layout(camera, handle, scale);
      const auto frame = extrusion_overlay(camera, handle, scale);
      ASSERT_FALSE(frame.triangles.empty());
      ASSERT_EQ(frame.lines.size(), 1u);
      EXPECT_TRUE(frame.points.empty());
      EXPECT_TRUE(frame.icons.empty());
      expect_overlay_color(frame, kDiskColor);
      EXPECT_FLOAT_EQ(frame.lines.front().width_pixels, scale);
      expect_vector_near(frame.lines.front().start, layout.pivot);
      const QVec3 facing = cross(layout.camera_right, layout.camera_up);
      for (const auto &triangle : frame.triangles) {
        EXPECT_EQ(triangle.layer, ViewportOverlayLayer::ToolHandle);
        EXPECT_EQ(triangle.source, ViewportOverlaySource::Tool);
        EXPECT_EQ(triangle.style_role, ViewportOverlayStyleRole::ExplicitColor);
        for (const QVec3 point : {triangle.a, triangle.b, triangle.c}) {
          EXPECT_NEAR(dot(point - layout.button_center, facing), 0.0F, 0.0001F);
          const auto projected = camera.project_world_point(point);
          ASSERT_TRUE(projected.visible);
          EXPECT_LE(pixel_length(projected.position - layout.button_center_screen),
                    layout.button_radius_pixels + 0.05F);
        }
      }
    }
  }
}

TEST(ExtrudeHandleLayoutTests, HoverTurnsWhiteAndDraggingHidesTheWholeAffordance) {
  const auto camera = extrusion_camera();
  for (const auto mode : kExtrusionModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto handle = extrusion_handle(mode);
    const auto idle = extrusion_overlay(camera, handle);
    expect_overlay_color(idle, kDiskColor);
    handle.new_hovered = true;
    const auto hovered = extrusion_overlay(camera, handle);
    expect_overlay_color(hovered, kActiveDiskColor);
    handle.new_hovered = false;
    handle.dragging = true;
    const auto dragging = extrusion_overlay(camera, handle);
    const auto dragging_layout = build_extrude_handle_layout(camera, handle);
    EXPECT_FALSE(dragging_layout.valid);
    EXPECT_EQ(hit_extrude_handle(dragging_layout, {}), ExtrudeHandlePart::None);
    EXPECT_TRUE(dragging.triangles.empty());
    EXPECT_TRUE(dragging.lines.empty());
    EXPECT_TRUE(dragging.points.empty());
    EXPECT_TRUE(dragging.icons.empty());
    EXPECT_EQ(idle.triangles.size(), hovered.triangles.size());
    handle.dragging = false;
    handle.world_position = {0.25F, 0.5F, 0.0F};
    const auto completed_layout = build_extrude_handle_layout(camera, handle);
    ASSERT_TRUE(completed_layout.valid);
    expect_vector_near(completed_layout.pivot, handle.world_position);
    expect_vector_near(completed_layout.button_center - completed_layout.pivot,
                       handle.world_direction * 12.0F *
                           completed_layout.button_world_radius);
    expect_overlay_color(extrusion_overlay(camera, handle), kDiskColor);
  }
}

TEST(ExtrudeHandleLayoutTests, HoverAndDragInvalidateTheOtherwiseStablePresentationStamp) {
  const auto camera = extrusion_camera();
  const auto overlay_camera =
      ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  auto handle = extrusion_handle(modeling::FaceExtrudeMode::Region);
  const ViewportToolOverlaySourceInputs inputs{
      .overlay_camera = &overlay_camera,
      .viewport_camera = &camera,
      .extrusion_handle = &handle};
  const auto stamp = [&]() {
    return ViewportToolOverlaySource(inputs).source_stamp();
  };
  const auto idle = stamp();
  EXPECT_EQ(idle, stamp());
  for (bool *flag : {&handle.new_hovered, &handle.dragging}) {
    *flag = true;
    const auto changed = stamp();
    EXPECT_NE(idle, changed);
    EXPECT_EQ(changed, stamp());
    *flag = false;
    EXPECT_EQ(idle, stamp());
  }
  handle.adjustment_available = true;
  handle.adjustment_hovered = true;
  handle.adjusting = true;
  EXPECT_EQ(idle, stamp());
  EXPECT_EQ(handle.revision, 0u);
}

TEST(ExtrudeHandleLayoutTests, FilledDiskReachesTheRendererAsToolGeometry) {
  const auto camera = extrusion_camera();
  const auto frame =
      extrusion_overlay(camera, extrusion_handle(modeling::FaceExtrudeMode::Region));
  ASSERT_FALSE(frame.triangles.empty());
  const auto plan = OverlayRenderableBuilder::build_layer(
      frame, ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size()),
      ViewportOverlayLayer::ToolHandle);
  ASSERT_EQ(plan.face_fill_batches.size(), 1u);
  const auto &batch = plan.face_fill_batches.front();
  EXPECT_EQ(batch.mesh.indices.size(), frame.triangles.size() * 3u);
  EXPECT_FALSE(batch.state.depth_culling);
  EXPECT_FALSE(batch.state.depth_write);
  EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
  EXPECT_EQ(batch.state.culling_mode, OverlayCullingMode::None);
  EXPECT_TRUE(plan.point_batches.empty());
  EXPECT_TRUE(plan.icon_batches.empty());
}

} // namespace
} // namespace quader

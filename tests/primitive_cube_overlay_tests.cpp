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
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/overlay/viewport_primitive_creation_overlay_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/primitive_creation_controls.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace quader {
namespace {

constexpr std::size_t kDotTriangles = 96u;
constexpr std::size_t kButtonTriangles = 38u;
constexpr std::size_t kMinusButtonTriangles = 34u;
constexpr std::size_t kAllTriangles =
    6u * kDotTriangles + 3u * kButtonTriangles;

ViewportOverlayCamera cube_camera() {
  ViewportOverlayCamera camera;
  camera.eye = {6.0f, 5.0f, 8.0f};
  camera.forward = normalize_or_zero(camera.eye * -1.0f);
  camera.right = normalize_or_zero(cross(camera.forward, {0.0f, 1.0f, 0.0f}));
  camera.viewport_size = {800.0f, 600.0f};
  return OverlayCameraProjector::sanitized_camera(camera);
}

QVec2 screen_point(const OverlayCameraProjector &projector, QVec3 world) {
  const OverlayProjectedPoint projected =
      projector.project(projector.view_point_for(world));
  // The renderer's projector uses bottom-left pixels; controls use top-left.
  return {projected.pixel.x,
          projector.camera().viewport_size.y - projected.pixel.y};
}

PrimitiveCreationControlsFrame cube_controls(const ViewportOverlayCamera &camera) {
  constexpr std::array<QVec3, 9> kCenters{{
      {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
      {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
      {0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f},
  }};
  const OverlayCameraProjector projector(camera);
  PrimitiveCreationControlsFrame controls;
  controls.visible = true;
  controls.revision = 1u;
  for (std::size_t index = 0u; index < kCenters.size(); ++index) {
    controls.controls[index] = {
        .world_position = kCenters[index],
        .screen_position = screen_point(projector, kCenters[index]),
        .depth = -projector.view_point_for(kCenters[index]).z,
        .radius_pixels = index < 6u ? 6.0f : 10.0f,
        .visible = true,
    };
  }
  return controls;
}

ViewportPrimitiveCreationState cylinder_overlay_state() {
  return {
      .object_id = 2,
      .local_min = {-2.0f, -3.0f, -2.0f},
      .local_max = {2.0f, 3.0f, 2.0f},
      .world_x = {1.0f, 0.0f, 0.0f},
      .world_y = {0.0f, 1.0f, 0.0f},
      .world_z = {0.0f, 0.0f, 1.0f},
      .revision = 1u,
      .shape = ViewportPrimitiveCreationShape::Cylinder,
      .side_count = 32,
      .caps = true,
  };
}

std::size_t color_count(const ViewportOverlayFrame &frame, std::uint32_t color) {
  return static_cast<std::size_t>(std::count_if(
      frame.triangles.begin(), frame.triangles.end(),
      [color](const ViewportOverlayTriangle &triangle) {
        return triangle.color == color;
      }));
}

std::optional<std::uint32_t> painted_color(
    const ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    QVec2 sample) {
  const OverlayCameraProjector projector(camera);
  std::optional<std::uint32_t> result;
  const auto side = [](QVec2 a, QVec2 b, QVec2 point) {
    return (b.x - a.x) * (point.y - a.y) -
           (b.y - a.y) * (point.x - a.x);
  };
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    const QVec2 a = screen_point(projector, triangle.a);
    const QVec2 b = screen_point(projector, triangle.b);
    const QVec2 c = screen_point(projector, triangle.c);
    const float ab = side(a, b, sample);
    const float bc = side(b, c, sample);
    const float ca = side(c, a, sample);
    if ((ab >= -0.001f && bc >= -0.001f && ca >= -0.001f) ||
        (ab <= 0.001f && bc <= 0.001f && ca <= 0.001f)) {
      result = triangle.color;
    }
  }
  return result;
}

void expect_control_extent(const ViewportOverlayFrame &frame,
                           const ViewportOverlayCamera &camera,
                           const PrimitiveCreationControl &control) {
  ASSERT_FALSE(frame.triangles.empty());
  const OverlayCameraProjector projector(camera);
  QVec2 min{std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
  QVec2 max{std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    for (const QVec3 vertex : {triangle.a, triangle.b, triangle.c}) {
      const QVec2 screen = screen_point(projector, vertex);
      min.x = std::min(min.x, screen.x);
      min.y = std::min(min.y, screen.y);
      max.x = std::max(max.x, screen.x);
      max.y = std::max(max.y, screen.y);
      const QVec2 delta = screen - control.screen_position;
      EXPECT_LE(std::sqrt(delta.x * delta.x + delta.y * delta.y),
                control.radius_pixels + 0.01f);
      EXPECT_NEAR(-projector.view_point_for(vertex).z, control.depth, 0.0001f);
    }
  }
  EXPECT_NEAR(min.x, control.screen_position.x - control.radius_pixels, 0.01f);
  EXPECT_NEAR(min.y, control.screen_position.y - control.radius_pixels, 0.01f);
  EXPECT_NEAR(max.x, control.screen_position.x + control.radius_pixels, 0.01f);
  EXPECT_NEAR(max.y, control.screen_position.y + control.radius_pixels, 0.01f);
}

class CubeOverlayTarget final : public ViewportOverlaySubmissionTarget {
public:
  ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return availability;
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
                                 ViewportOverlayFrame,
                                 ViewportOverlayCamera) override {}
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
                              ViewportOverlayFrame frame,
                              ViewportOverlayCamera) override {
    ++dynamic_submissions;
    dynamic_frame = std::move(frame);
  }

  ViewportOverlayCameraUpdate availability{true, true};
  std::uint64_t dynamic_submissions = 0u;
  ViewportOverlayFrame dynamic_frame;
};

class CountingCubeSource final : public ViewportOverlayDynamicToolFrameProvider {
public:
  explicit CountingCubeSource(ViewportToolOverlaySourceInputs inputs)
      : source(inputs) {}

  ViewportOverlayFrame build_dynamic_tool_overlay_frame() const override {
    ++builds;
    return source.build_dynamic_tool_overlay_frame();
  }

  ViewportToolOverlaySource source;
  mutable std::uint64_t builds = 0u;
};

void expect_highlight_segments(const ViewportOverlayFrame &frame,
                               const PrimitiveCreationControlsFrame &controls,
                               float raster_scale = 1.0f) {
  const std::size_t count = std::min(controls.highlight_point_count,
                                    controls.highlight_points.size());
  ASSERT_EQ(frame.lines.size(), count > 0u ? count - 1u : 0u);
  for (std::size_t index = 0u; index < frame.lines.size(); ++index) {
    const auto &line = frame.lines[index];
    EXPECT_FLOAT_EQ(length_squared(line.start - controls.highlight_points[index]), 0.0f);
    EXPECT_FLOAT_EQ(length_squared(line.end - controls.highlight_points[index + 1u]), 0.0f);
    EXPECT_EQ(line.layer, ViewportOverlayLayer::ToolPreviewLine);
    EXPECT_EQ(line.source, ViewportOverlaySource::Tool);
    EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::ExplicitColor);
    EXPECT_EQ(line.color, kOverlayPrimitiveCubeSubdivisionHighlightColor);
    EXPECT_FLOAT_EQ(line.width_pixels, 2.0f * raster_scale);
  }
}

TEST(PrimitiveCubeOverlayTests, EmitsSixOutlinedRedDotsAndThreeYellowPlusesOnly) {
  const ViewportOverlayCamera camera = cube_camera();
  const PrimitiveCreationControlsFrame controls = cube_controls(camera);
  ViewportOverlayFrame frame;
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);

  ASSERT_EQ(frame.triangles.size(), kAllTriangles);
  EXPECT_TRUE(frame.lines.empty()) << "Cube controls have no axis lines";
  EXPECT_TRUE(frame.points.empty());
  EXPECT_TRUE(frame.icons.empty());
  EXPECT_EQ(color_count(frame, rgba8(230, 62, 62)), 6u * 32u);
  EXPECT_EQ(color_count(frame, rgba8(255, 255, 255)), 6u * 64u);
  EXPECT_EQ(color_count(frame, rgba8(255, 214, 0)), 3u * 32u);
  EXPECT_EQ(color_count(frame, rgba8(0, 0, 0)), 3u * 6u);
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    EXPECT_EQ(triangle.layer, ViewportOverlayLayer::ToolHandle);
    EXPECT_EQ(triangle.source, ViewportOverlaySource::Tool);
    EXPECT_EQ(triangle.style_role, ViewportOverlayStyleRole::ExplicitColor);
  }

  // Exercise the actual renderer's batch policy, including back-face dots.
  const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
      frame, camera, ViewportOverlayLayer::ToolHandle);
  ASSERT_EQ(plan.face_fill_batches.size(), 1u);
  const OverlayFaceFillBatch &batch = plan.face_fill_batches.front();
  EXPECT_EQ(batch.mesh.indices.size(), kAllTriangles * 3u);
  EXPECT_FALSE(batch.state.depth_culling);
  EXPECT_FALSE(batch.state.depth_write);
  EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
  EXPECT_EQ(batch.state.culling_mode, OverlayCullingMode::None);
  EXPECT_TRUE(plan.line_batches.empty());
  EXPECT_TRUE(plan.edit_wire_batches.empty());
  EXPECT_TRUE(plan.point_batches.empty());
  EXPECT_TRUE(plan.icon_batches.empty());
}

TEST(PrimitiveCubeOverlayTests,
     DiscCentersOutlinesAndPlusArmsOccupyTheirPublishedTargets) {
  const ViewportOverlayCamera camera = cube_camera();
  for (const std::size_t index : {0u, 6u}) {
    PrimitiveCreationControlsFrame controls;
    controls.visible = true;
    controls.controls[index] = {
        .screen_position = {400.0f, 300.0f},
        .depth = 10.0f,
        .radius_pixels = 10.0f,
        .visible = true,
    };
    ViewportOverlayFrame frame;
    ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
    const auto color = [&](QVec2 offset) {
      return painted_color(frame, camera,
                           controls.controls[index].screen_position + offset);
    };
    if (index < 6u) {
      EXPECT_EQ(color({0.0f, 0.0f}), kOverlayPrimitiveCubeFaceColor);
      EXPECT_EQ(color({3.0f, 3.0f}), kOverlayPrimitiveCubeFaceColor);
      for (const QVec2 offset :
           {QVec2{9.0f, 0.0f}, QVec2{-9.0f, 0.0f},
            QVec2{0.0f, 9.0f}, QVec2{0.0f, -9.0f}}) {
        EXPECT_EQ(color(offset), kOverlayPrimitiveCubeFaceOutlineColor);
      }
    } else {
      for (const QVec2 offset :
           {QVec2{0.0f, 0.0f}, QVec2{4.0f, 0.0f}, QVec2{-4.0f, 0.0f},
            QVec2{0.0f, 4.0f}, QVec2{0.0f, -4.0f}}) {
        EXPECT_EQ(color(offset), kOverlayPrimitiveCubeSubdivisionGlyphColor);
      }
      EXPECT_EQ(color({3.0f, 3.0f}), kOverlayPrimitiveCubeSubdivisionColor);
      EXPECT_EQ(color({8.0f, 0.0f}), kOverlayPrimitiveCubeSubdivisionColor);
    }
    EXPECT_FALSE(color({11.0f, 0.0f}).has_value());
    EXPECT_FALSE(color({0.0f, -11.0f}).has_value());
    expect_control_extent(frame, camera, controls.controls[index]);
  }
}

TEST(PrimitiveCubeOverlayTests,
     MixedPlusAndMinusButtonsReserveOnlyTheirEmittedTriangles) {
  const ViewportOverlayCamera camera = cube_camera();
  for (const unsigned minus_mask : {0u, 1u, 5u, 7u}) {
    SCOPED_TRACE(minus_mask);
    PrimitiveCreationControlsFrame controls = cube_controls(camera);
    std::size_t minus_count = 0u;
    for (std::size_t index = 0u; index < controls.controls.size(); ++index) {
      const bool decrement = index < 6u ||
                             (minus_mask & (1u << (index - 6u))) != 0u;
      controls.controls[index].subdivision_decrement = decrement;
      minus_count += index >= 6u && decrement ? 1u : 0u;
    }
    const std::size_t triangle_count =
        6u * kDotTriangles + minus_count * kMinusButtonTriangles +
        (3u - minus_count) * kButtonTriangles;
    ViewportOverlayFrame frame;
    frame.triangles.reserve(1u + triangle_count);
    frame.triangles.push_back({.color = rgba8(1, 2, 3)});
    const auto *storage = frame.triangles.data();
    const auto capacity = frame.triangles.capacity();
    ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
    ASSERT_EQ(frame.triangles.size(), 1u + triangle_count);
    EXPECT_EQ(frame.triangles.data(), storage);
    EXPECT_EQ(frame.triangles.capacity(), capacity)
        << "Exact emitted space must suffice without a second reservation";
    EXPECT_EQ(frame.triangles.front().color, rgba8(1, 2, 3));
    EXPECT_EQ(color_count(frame, rgba8(230, 62, 62)), 6u * 32u);
    EXPECT_EQ(color_count(frame, rgba8(255, 255, 255)), 6u * 64u);
    EXPECT_EQ(color_count(frame, rgba8(255, 214, 0)), 3u * 32u);
    EXPECT_EQ(color_count(frame, rgba8(0, 0, 0)),
              minus_count * 2u + (3u - minus_count) * 6u);
    EXPECT_TRUE(frame.lines.empty());
    EXPECT_TRUE(frame.points.empty());
    EXPECT_TRUE(frame.icons.empty());
  }
}

TEST(PrimitiveCubeOverlayTests,
     MinusKeepsHorizontalBarAndYellowFormerVerticalArms) {
  const ViewportOverlayCamera camera = cube_camera();
  for (const std::size_t index : {6u, 7u, 8u}) {
    for (const int state : {0, 1, 2, 3}) {
      SCOPED_TRACE(::testing::Message() << "control=" << index << " state=" << state);
      PrimitiveCreationControlsFrame controls;
      controls.visible = true;
      PrimitiveCreationControl &control = controls.controls[index];
      control.screen_position = {400.0f, 300.0f};
      control.depth = 10.0f;
      control.radius_pixels = 10.0f;
      control.visible = true;
      control.enabled = state != 3;
      control.subdivision_decrement = true;
      controls.hovered = state == 1 || state == 3 ? static_cast<int>(index) : -1;
      controls.active = state == 2 ? static_cast<int>(index) : -1;
      const std::uint32_t fill =
          state == 3 ? kOverlayPrimitiveCubeSubdivisionDisabledColor
          : state == 0 ? kOverlayPrimitiveCubeSubdivisionColor
                       : kOverlayPrimitiveCubeSubdivisionHighlightColor;
      ViewportOverlayFrame frame;
      ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
      ASSERT_EQ(frame.triangles.size(), kMinusButtonTriangles);
      EXPECT_EQ(color_count(frame, fill), 32u);
      EXPECT_EQ(color_count(frame, rgba8(0, 0, 0)), 2u);
      const auto color = [&](QVec2 offset) {
        return painted_color(frame, camera, control.screen_position + offset);
      };
      for (const QVec2 offset :
           {QVec2{0.0f, 0.0f}, QVec2{-4.0f, 0.0f}, QVec2{4.0f, 0.0f}}) {
        EXPECT_EQ(color(offset), rgba8(0, 0, 0));
      }
      for (const QVec2 offset :
           {QVec2{0.0f, -4.0f}, QVec2{0.0f, 4.0f}, QVec2{3.0f, 3.0f},
            QVec2{-8.0f, 0.0f}, QVec2{8.0f, 0.0f}}) {
        EXPECT_EQ(color(offset), fill);
      }
      EXPECT_FALSE(color({11.0f, 0.0f}).has_value());
      expect_control_extent(frame, camera, control);
      const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
          frame, camera, ViewportOverlayLayer::ToolHandle);
      ASSERT_EQ(plan.face_fill_batches.size(), 1u);
      EXPECT_EQ(plan.face_fill_batches.front().mesh.indices.size(),
                kMinusButtonTriangles * 3u);
      EXPECT_FALSE(plan.face_fill_batches.front().state.depth_write);
      EXPECT_EQ(plan.face_fill_batches.front().state.depth_func,
                OverlayDepthFunc::Always);
    }
  }
}

TEST(PrimitiveCubeOverlayTests,
     PublishedScreenTargetsAndRadiiSurviveProjectionDepthAndDpr) {
  for (const ViewportOverlayProjection projection :
       {ViewportOverlayProjection::Perspective,
        ViewportOverlayProjection::Orthographic}) {
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
      for (const float depth : {1.0f, 17.0f, 60.0f}) {
        for (const std::size_t index : {0u, 6u, 7u}) {
          SCOPED_TRACE(::testing::Message()
                       << "projection=" << static_cast<int>(projection)
                       << " scale=" << scale << " depth=" << depth
                       << " control=" << index);
          ViewportOverlayCamera camera = cube_camera();
          camera.projection = projection;
          camera.viewport_size = {800.0f * scale, 600.0f * scale};
          PrimitiveCreationControlsFrame controls;
          controls.visible = true;
          PrimitiveCreationControl &control = controls.controls[index];
          control = {
              // Published screen placement is authoritative even when an
              // interaction freezes/offsets a button away from this anchor.
              .world_position = {200.0f, -100.0f, 900.0f},
              .screen_position = {234.5f * scale, 165.25f * scale},
              .depth = depth,
              .radius_pixels = (index < 6u ? 6.0f : 10.0f) * scale,
              .visible = true,
          };
          control.subdivision_decrement = index == 7u;
          controls.hovered = static_cast<int>(index);
          const ViewportToolOverlaySource source({
              .raster_scale = scale,
              .overlay_camera = &camera,
              .primitive_creation_controls = &controls,
              .interaction_pane = true,
          });
          ViewportOverlayFrame frame = source.build_dynamic_tool_overlay_frame();
          ASSERT_EQ(frame.triangles.size(),
                    index < 6u ? kDotTriangles
                    : control.subdivision_decrement ? kMinusButtonTriangles
                                                    : kButtonTriangles);
          ViewportOverlayStyleResolver::apply(
              frame, ViewportSettings::defaults(), {.raster_scale = scale});
          expect_control_extent(frame, camera, control);
          const std::uint32_t highlight =
              index < 6u ? kOverlayPrimitiveCubeFaceHighlightColor
                         : kOverlayPrimitiveCubeSubdivisionHighlightColor;
          EXPECT_EQ(color_count(frame, highlight), 32u);
          if (index >= 6u) {
            const auto color = [&](QVec2 offset) {
              return painted_color(frame, camera,
                                   control.screen_position + offset * scale);
            };
            EXPECT_EQ(color({-4.0f, 0.0f}),
                      kOverlayPrimitiveCubeSubdivisionGlyphColor);
            EXPECT_EQ(color({4.0f, 0.0f}),
                      kOverlayPrimitiveCubeSubdivisionGlyphColor);
            EXPECT_EQ(color({4.0f, 0.8f}),
                      kOverlayPrimitiveCubeSubdivisionGlyphColor);
            EXPECT_EQ(color({5.6f, 0.0f}), highlight);
            EXPECT_EQ(color({0.0f, -4.0f}),
                      control.subdivision_decrement
                          ? highlight : kOverlayPrimitiveCubeSubdivisionGlyphColor);
            EXPECT_EQ(color({0.0f, 4.0f}),
                      control.subdivision_decrement
                          ? highlight : kOverlayPrimitiveCubeSubdivisionGlyphColor);
            EXPECT_EQ(color({0.0f, 1.5f}),
                      control.subdivision_decrement
                          ? highlight : kOverlayPrimitiveCubeSubdivisionGlyphColor);
          }
        }
      }
    }
  }
}

TEST(PrimitiveCubeOverlayTests,
     PressedPlusKeepsWorldAnchorWhileCameraAndDpiReprojectItsHitAndRenderTarget) {
  for (const std::size_t index : {6u, 7u, 8u}) {
    SCOPED_TRACE(index);
    ViewportPrimitiveCreationState cube{
        .object_id = 1,
        .local_min = {-1.0f, -1.0f, -1.0f},
        .local_max = {1.0f, 1.0f, 1.0f},
        .world_x = {1.0f, 0.0f, 0.0f},
        .world_y = {0.0f, 1.0f, 0.0f},
        .world_z = {0.0f, 0.0f, 1.0f},
        .revision = 1u,
    };
    ViewportCameraController camera;
    camera.set_viewport_size(800.0f, 600.0f);
    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = {6.0f, 5.0f, 8.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
    PrimitiveCreationInteraction interaction;
    const PrimitiveCreationControl original =
        interaction.project(cube, camera, 1.0f).controls[index];
    ASSERT_TRUE(original.visible);
    interaction.begin(static_cast<int>(index), cube, camera,
                      original.screen_position);

    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = {-9.0f, -7.0f, -13.0f},
         .target = {},
         .up = {0.0f, 1.0f, 0.0f}}));
    PrimitiveCreationInteraction unpressed;
    const PrimitiveCreationControl other_edge =
        unpressed.project(cube, camera, 1.0f).controls[index];
    ASSERT_GT(length_squared(other_edge.world_position - original.world_position),
              0.0f)
        << "The new camera must prefer a different edge";
    const PrimitiveCreationControl camera_changed =
        interaction.project(cube, camera, 1.0f).controls[index];
    const ViewportScreenPoint expected =
        camera.project_world_point(original.world_position);
    EXPECT_FLOAT_EQ(camera_changed.screen_position.x, expected.position.x);
    EXPECT_FLOAT_EQ(camera_changed.screen_position.y, expected.position.y);
    EXPECT_FLOAT_EQ(camera_changed.depth, expected.depth);
    EXPECT_NE(camera_changed.depth, original.depth);

    camera.set_viewport_size(1600.0f, 1200.0f);
    const PrimitiveCreationControlsFrame scaled =
        interaction.project(cube, camera, 2.0f);
    const PrimitiveCreationControl &button = scaled.controls[index];
    ASSERT_TRUE(button.visible);
    EXPECT_FLOAT_EQ(
        length_squared(button.world_position - original.world_position), 0.0f);
    EXPECT_FLOAT_EQ(button.screen_position.x,
                    camera_changed.screen_position.x * 2.0f);
    EXPECT_FLOAT_EQ(button.screen_position.y,
                    camera_changed.screen_position.y * 2.0f);
    EXPECT_FLOAT_EQ(button.radius_pixels, 18.0f);
    EXPECT_EQ(interaction.hit_test(button.screen_position),
              static_cast<int>(index));

    const ViewportOverlayCamera overlay_camera =
        ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
    ViewportOverlayFrame overlay;
    ViewportPrimitiveCreationOverlayBuilder::append(overlay, overlay_camera, scaled);
    ASSERT_EQ(overlay.triangles.size(), kAllTriangles);
    // The active control is painted last, above overlapping controls.
    ViewportOverlayFrame active_overlay;
    active_overlay.triangles.assign(
        overlay.triangles.end() - static_cast<std::ptrdiff_t>(kButtonTriangles),
        overlay.triangles.end());
    expect_control_extent(active_overlay, overlay_camera, button);
    const auto builds = interaction.projection_count();
    EXPECT_EQ(interaction.project(cube, camera, 2.0f).revision, scaled.revision);
    EXPECT_EQ(interaction.projection_count(), builds);

    cube.subdivisions[index - 6u] = 128;
    ++cube.revision;
    EXPECT_FALSE(
        interaction.project(cube, camera, 2.0f).controls[index].enabled);
    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = {0.0f, 0.0f, -10.0f},
         .target = {0.0f, 0.0f, -20.0f},
         .up = {0.0f, 1.0f, 0.0f}}));
    const PrimitiveCreationControlsFrame clipped =
        interaction.project(cube, camera, 2.0f);
    EXPECT_FALSE(clipped.controls[index].visible);
    EXPECT_EQ(interaction.hit_test(button.screen_position), -1);
    overlay = {};
    ViewportPrimitiveCreationOverlayBuilder::append(
        overlay,
        ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size()),
        clipped);
    EXPECT_TRUE(overlay.triangles.empty());

    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = {-9.0f, -7.0f, -13.0f},
         .target = {},
         .up = {0.0f, 1.0f, 0.0f}}));
    interaction.end();
    const PrimitiveCreationControl released =
        interaction.project(cube, camera, 2.0f).controls[index];
    EXPECT_FLOAT_EQ(
        length_squared(released.world_position - other_edge.world_position),
        0.0f) << "Release restores camera-based edge choice";
  }
}

TEST(PrimitiveCubeOverlayTests,
     HoverAndActiveHighlightWithoutGrowingOrDuplicatingControls) {
  const ViewportOverlayCamera camera = cube_camera();
  PrimitiveCreationControlsFrame controls = cube_controls(camera);
  controls.hovered = 0;
  controls.active = 1;
  ViewportOverlayFrame frame;
  ViewportToolOverlayProjection::append_primitive_creation_controls(frame, camera,
                                                                controls);
  ASSERT_EQ(frame.triangles.size(), kAllTriangles);
  EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeFaceHighlightColor), 64u);
  EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeFaceColor), 128u);
  const OverlayCameraProjector projector(camera);
  const QVec2 active_center = screen_point(
      projector, frame.triangles[frame.triangles.size() - kDotTriangles].a);
  EXPECT_NEAR(active_center.x, controls.controls[1].screen_position.x, 0.001f);
  EXPECT_NEAR(active_center.y, controls.controls[1].screen_position.y, 0.001f);

  controls.hovered = 6;
  controls.active = 6;
  frame = {};
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
  EXPECT_EQ(frame.triangles.size(), kAllTriangles);
  EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeSubdivisionHighlightColor),
            32u);
  EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeFaceHighlightColor), 0u);

  controls.controls[6].enabled = false;
  frame = {};
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
  EXPECT_EQ(frame.triangles.size(), kAllTriangles);
  EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeSubdivisionHighlightColor),
            0u);
  EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeSubdivisionDisabledColor),
            32u);
  EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeSubdivisionGlyphColor), 18u);
}

TEST(PrimitiveCubeOverlayTests, HiddenOrInvalidControlsDoNotAppendGeometry) {
  const ViewportOverlayCamera camera = cube_camera();
  PrimitiveCreationControlsFrame controls = cube_controls(camera);
  controls.visible = false;
  ViewportOverlayFrame frame;
  frame.triangles.push_back({.color = rgba8(1, 2, 3)});
  const std::size_t capacity = frame.triangles.capacity();
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
  ASSERT_EQ(frame.triangles.size(), 1u);
  EXPECT_EQ(frame.triangles.front().color, rgba8(1, 2, 3));
  EXPECT_EQ(frame.triangles.capacity(), capacity);

  controls.visible = true;
  controls.controls[0].visible = false;
  controls.controls[1].radius_pixels = 0.0f;
  controls.controls[2].radius_pixels =
      std::numeric_limits<float>::quiet_NaN();
  controls.controls[3].screen_position.x =
      std::numeric_limits<float>::infinity();
  controls.controls[4].depth = -1.0f;
  controls.controls[5].depth = std::numeric_limits<float>::quiet_NaN();
  controls.hovered = 123;
  controls.active = -5;
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
  EXPECT_EQ(frame.triangles.size(), 1u + 3u * kButtonTriangles);
  EXPECT_TRUE(frame.lines.empty());
}

TEST(PrimitiveCubeOverlayTests,
     SourceUsesOwnerRevisionAndVisibleCameraWithoutHashingControlGeometry) {
  ViewportOverlayCamera camera = cube_camera();
  PrimitiveCreationControlsFrame controls = cube_controls(camera);
  ViewportToolOverlaySourceInputs inputs{
      .source_identity = 41u,
      .overlay_camera = &camera,
      .primitive_creation_controls = &controls,
      .interaction_pane = true,
  };
  const auto stamp = [&] {
    return ViewportToolOverlaySource(inputs).source_stamp();
  };
  const auto initial = stamp();
  EXPECT_EQ(stamp(), initial);
  for (PrimitiveCreationControl &control : controls.controls) {
    control.world_position.x += 5.0f;
    control.screen_position.x += 10.0f;
    control.radius_pixels += 2.0f;
  }
  controls.hovered = 3;
  EXPECT_EQ(stamp(), initial)
      << "Geometry and interaction are invalidated by the owner's revision";
  ++controls.revision;
  const auto revised = stamp();
  EXPECT_NE(revised, initial);
  camera.eye.x += 1.0f;
  EXPECT_NE(stamp(), revised);
  const auto moved = stamp();
  inputs.raster_scale = 2.0f;
  EXPECT_NE(stamp(), moved);

  controls.visible = false;
  ++controls.revision;
  const auto hidden = stamp();
  camera.eye.y += 1.0f;
  EXPECT_EQ(stamp(), hidden) << "An empty source is camera independent";
  EXPECT_TRUE(ViewportToolOverlaySource(inputs)
                  .build_dynamic_tool_overlay_frame().triangles.empty());
  controls.visible = true;
  inputs.interaction_pane = false;
  const auto other_pane = stamp();
  EXPECT_EQ(stamp(), other_pane);
  controls.controls[0].world_position.y += 1.0f;
  controls.hovered = 2;
  EXPECT_EQ(stamp(), other_pane)
      << "Shared pane geometry and feedback still use the owner's revision";
  ++controls.revision;
  const auto other_pane_revised = stamp();
  EXPECT_NE(other_pane_revised, other_pane);
  camera.eye.z += 1.0f;
  EXPECT_NE(stamp(), other_pane_revised)
      << "Shared controls are reprojected using each visible pane's camera";
  const auto other_pane_moved = stamp();
  for (int frame = 0; frame < 12; ++frame) EXPECT_EQ(stamp(), other_pane_moved);
  EXPECT_EQ(ViewportToolOverlaySource(inputs)
                .build_dynamic_tool_overlay_frame().triangles.size(), kAllTriangles);
  inputs.interaction_pane = true;
  inputs.overlay_camera = nullptr;
  EXPECT_TRUE(ViewportToolOverlaySource(inputs)
                  .build_dynamic_tool_overlay_frame().triangles.empty());
}

TEST(PrimitiveCubeOverlayTests,
     SubdivisionGlyphRevisionInvalidatesOnceAndUnchangedFramesStayCached) {
  const ViewportOverlayCamera camera = cube_camera();
  PrimitiveCreationControlsFrame controls = cube_controls(camera);
  const ViewportToolOverlaySourceInputs inputs{
      .source_identity = 43u,
      .overlay_camera = &camera,
      .primitive_creation_controls = &controls,
      .interaction_pane = true,
  };
  ViewportOverlaySubmissionCoordinator coordinator;
  CubeOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  std::uint64_t total_builds = 0u;
  const auto submit = [&] {
    const CountingCubeSource provider(inputs);
    const auto result = coordinator.submit(
        {.layer_mask = 1u,
         .camera = camera,
         .dynamic_tool_source = provider.source.source_stamp(),
         .dynamic_tool_frame_provider = &provider},
        settings, target);
    total_builds += provider.builds;
    return result;
  };
  ASSERT_TRUE(submit().dynamic_submitted);
  ASSERT_EQ(target.dynamic_frame.triangles.size(), kAllTriangles);
  for (std::size_t index = 6u; index < controls.controls.size(); ++index) {
    controls.controls[index].subdivision_decrement = true;
  }
  EXPECT_FALSE(submit().dynamic_submitted)
      << "The glyph is invalidated by the owner revision, never by hashing controls";
  EXPECT_EQ(total_builds, 1u);
  EXPECT_EQ(target.dynamic_frame.triangles.size(), kAllTriangles);
  ++controls.revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(target.dynamic_frame.triangles.size(),
            6u * kDotTriangles + 3u * kMinusButtonTriangles);
  EXPECT_EQ(color_count(target.dynamic_frame,
                        kOverlayPrimitiveCubeSubdivisionGlyphColor), 6u);
  const auto composed_records =
      coordinator.work_counters().dynamic_source_composed_record_count;
  for (int frame = 0; frame < 120; ++frame) {
    EXPECT_FALSE(submit().dynamic_submitted);
  }
  EXPECT_EQ(total_builds, 2u);
  EXPECT_EQ(target.dynamic_submissions, 2u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, 2u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_composed_record_count,
            composed_records);
  for (std::size_t index = 6u; index < controls.controls.size(); ++index) {
    controls.controls[index].subdivision_decrement = false;
  }
  ++controls.revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(target.dynamic_frame.triangles.size(), kAllTriangles);
  EXPECT_EQ(color_count(target.dynamic_frame,
                        kOverlayPrimitiveCubeSubdivisionGlyphColor), 18u);
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 3u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hash_count, 0u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hashed_record_count, 0u);
}

TEST(PrimitiveCubeOverlayTests,
     SubmissionCachesUnchangedFramesAndRecoversWithoutRebuildingSource) {
  ViewportOverlayCamera camera = cube_camera();
  PrimitiveCreationControlsFrame controls = cube_controls(camera);
  const ViewportToolOverlaySourceInputs inputs{
      .source_identity = 42u,
      .overlay_camera = &camera,
      .primitive_creation_controls = &controls,
      .interaction_pane = true,
  };
  ViewportOverlaySubmissionCoordinator coordinator;
  CubeOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  std::uint64_t total_builds = 0u;
  const auto submit = [&] {
    const CountingCubeSource provider(inputs);
    const auto result = coordinator.submit(
        {.layer_mask = 1u,
         .camera = camera,
         .dynamic_tool_source = provider.source.source_stamp(),
         .dynamic_tool_frame_provider = &provider},
        settings, target);
    total_builds += provider.builds;
    return result;
  };
  EXPECT_TRUE(submit().dynamic_submitted);
  ASSERT_EQ(target.dynamic_frame.triangles.size(), kAllTriangles);
  const std::uint64_t initial_records =
      coordinator.work_counters().dynamic_source_composed_record_count;
  for (int frame = 0; frame < 120; ++frame) {
    EXPECT_FALSE(submit().dynamic_submitted);
  }
  EXPECT_EQ(total_builds, 1u);
  EXPECT_EQ(target.dynamic_submissions, 1u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, 1u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_composed_record_count,
            initial_records);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hash_count, 0u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hashed_record_count, 0u);

  controls.hovered = 2;
  ++controls.revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 2u);
  EXPECT_EQ(color_count(target.dynamic_frame,
                        kOverlayPrimitiveCubeFaceHighlightColor), 32u);
  camera.eye.x += 1.0f;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 3u);

  target.availability.dynamic_available = false;
  const auto recovered = submit();
  EXPECT_TRUE(recovered.dynamic_submitted);
  EXPECT_TRUE(recovered.dynamic_recovered);
  EXPECT_EQ(total_builds, 3u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, 3u);
  target.availability.dynamic_available = true;

  controls.visible = false;
  ++controls.revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.dynamic_frame.triangles.empty());
  EXPECT_EQ(total_builds, 4u);
  camera.eye.y += 1.0f;
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 4u);
}

TEST(PrimitiveCubeOverlayTests,
     CylinderProjectionRendersThreeDotsAndOneDpiSizedSegmentGlyph) {
  for (const QVec3 eye : {QVec3{8.0f, 7.0f, 11.0f},
                          QVec3{-11.0f, 8.0f, -7.0f}}) {
    for (const bool caps : {true, false}) {
      for (const float scale : {1.0f, 1.5f, 2.0f}) {
        for (const bool decrement : {false, true}) {
          SCOPED_TRACE(::testing::Message()
                       << "eye.x=" << eye.x << " caps=" << caps
                       << " scale=" << scale << " decrement=" << decrement);
          auto cylinder = cylinder_overlay_state();
          cylinder.caps = caps;
          ViewportCameraController camera;
          camera.set_viewport_size(800.0f * scale, 600.0f * scale);
          ASSERT_TRUE(camera.restore_perspective_pose(
              {.eye = eye, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
          PrimitiveCreationInteraction interaction;
          const PrimitiveCreationControlsFrame &controls =
              interaction.project(cylinder, camera, scale);
          interaction.set_subdivision_decrement(decrement);
          if (decrement) interaction.hover(6);
          ASSERT_TRUE(controls.visible);
          for (std::size_t index = 0u; index < controls.controls.size(); ++index) {
            EXPECT_EQ(controls.controls[index].visible,
                      index == 0u || index == 2u || index == 3u || index == 6u);
          }
          // The renderer must use the interaction's camera-facing side and
          // upper-rim targets, including when the cylinder has no caps.
          for (const std::size_t index : {0u, 6u}) {
            const QVec3 anchor = controls.controls[index].world_position;
            EXPECT_NEAR(anchor.x * anchor.x + anchor.z * anchor.z, 4.0f, 0.001f);
            EXPECT_NEAR(anchor.y, index == 0u ? 0.0f : 3.0f, 0.001f);
            EXPECT_GT(anchor.x * eye.x + anchor.z * eye.z, 0.0f);
          }
          const ViewportOverlayCamera overlay_camera =
              ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
          const ViewportToolOverlaySource source({
              .raster_scale = scale,
              .overlay_camera = &overlay_camera,
              .primitive_creation_controls = &controls,
              .interaction_pane = true,
          });
          ViewportOverlayFrame frame = source.build_dynamic_tool_overlay_frame();
          const std::size_t button_triangles =
              decrement ? kMinusButtonTriangles : kButtonTriangles;
          ASSERT_EQ(frame.triangles.size(), 3u * kDotTriangles + button_triangles);
          ViewportOverlayStyleResolver::apply(
              frame, ViewportSettings::defaults(), {.raster_scale = scale});
          EXPECT_EQ(color_count(frame, rgba8(230, 62, 62)), 3u * 32u);
          EXPECT_EQ(color_count(frame, rgba8(255, 255, 255)), 3u * 64u);
          const auto button_fill = decrement
                                       ? kOverlayPrimitiveCubeSubdivisionHighlightColor
                                       : kOverlayPrimitiveCubeSubdivisionColor;
          EXPECT_EQ(color_count(frame, button_fill), 32u);
          EXPECT_EQ(color_count(frame, rgba8(0, 0, 0)), decrement ? 2u : 6u);
          expect_highlight_segments(frame, controls, scale);
          EXPECT_TRUE(frame.points.empty());
          EXPECT_TRUE(frame.icons.empty());
          std::size_t first_triangle = 0u;
          for (const std::size_t index : {0u, 2u, 3u, 6u}) {
            const auto &control = controls.controls[index];
            const bool button = index == 6u;
            EXPECT_FLOAT_EQ(control.radius_pixels, (button ? 9.0f : 6.0f) * scale);
            const std::size_t count = button ? button_triangles : kDotTriangles;
            ViewportOverlayFrame piece;
            piece.triangles.assign(
                frame.triangles.begin() + static_cast<std::ptrdiff_t>(first_triangle),
                frame.triangles.begin() + static_cast<std::ptrdiff_t>(first_triangle + count));
            first_triangle += count;
            expect_control_extent(piece, overlay_camera, control);
            const auto color = [&](QVec2 relative_offset) {
              return painted_color(piece, overlay_camera,
                  control.screen_position + relative_offset * control.radius_pixels);
            };
            if (button) {
              EXPECT_EQ(control.subdivision_decrement, decrement);
              EXPECT_EQ(color({-0.4f, 0.0f}), rgba8(0, 0, 0));
              EXPECT_EQ(color({0.4f, 0.0f}), rgba8(0, 0, 0));
              EXPECT_EQ(color({0.0f, -0.4f}),
                        decrement ? button_fill : rgba8(0, 0, 0));
              EXPECT_EQ(color({0.0f, 0.4f}),
                        decrement ? button_fill : rgba8(0, 0, 0));
              EXPECT_EQ(color({0.8f, 0.0f}), button_fill);
            } else {
              EXPECT_EQ(color({0.0f, 0.0f}), rgba8(230, 62, 62));
              EXPECT_EQ(color({0.9f, 0.0f}), rgba8(255, 255, 255));
            }
          }
          const auto plan = OverlayRenderableBuilder::build_layer(
              frame, overlay_camera, ViewportOverlayLayer::ToolHandle);
          ASSERT_EQ(plan.face_fill_batches.size(), 1u);
          const auto &batch = plan.face_fill_batches.front();
          EXPECT_EQ(batch.mesh.indices.size(), frame.triangles.size() * 3u);
          EXPECT_FALSE(batch.state.depth_culling);
          EXPECT_FALSE(batch.state.depth_write);
          EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
        }
      }
    }
  }
}

TEST(PrimitiveCubeOverlayTests,
     CylinderInteractionRevisionGatesOverlayAndUnchangedProjectionWork) {
  const auto cylinder = cylinder_overlay_state();
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  ASSERT_TRUE(camera.restore_perspective_pose(
      {.eye = {8.0f, 7.0f, 11.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
  PrimitiveCreationInteraction interaction;
  ViewportOverlaySubmissionCoordinator coordinator;
  CubeOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  std::uint64_t total_builds = 0u;
  const auto submit = [&] {
    const auto &controls = interaction.project(cylinder, camera, 1.0f);
    const auto overlay_camera =
        ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
    const CountingCubeSource provider({
        .source_identity = 44u,
        .overlay_camera = &overlay_camera,
        .primitive_creation_controls = &controls,
        .interaction_pane = true,
    });
    const auto result = coordinator.submit(
        {.layer_mask = 1u,
         .camera = overlay_camera,
         .dynamic_tool_source = provider.source.source_stamp(),
         .dynamic_tool_frame_provider = &provider},
        settings, target);
    total_builds += provider.builds;
    return result;
  };
  ASSERT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(target.dynamic_frame.triangles.size(), 3u * kDotTriangles + kButtonTriangles);
  const auto projections = interaction.projection_count();
  const auto initial_revision = interaction.frame().revision;
  for (int frame = 0; frame < 120; ++frame) {
    EXPECT_FALSE(submit().dynamic_submitted);
  }
  EXPECT_EQ(interaction.frame().revision, initial_revision);
  EXPECT_EQ(interaction.projection_count(), projections);
  EXPECT_EQ(total_builds, 1u);

  interaction.set_subdivision_decrement(true);
  EXPECT_NE(interaction.frame().revision, initial_revision);
  ASSERT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(target.dynamic_frame.triangles.size(),
            3u * kDotTriangles + kMinusButtonTriangles);
  EXPECT_EQ(color_count(target.dynamic_frame, rgba8(0, 0, 0)), 2u);
  const auto minus_revision = interaction.frame().revision;
  const auto composed_records =
      coordinator.work_counters().dynamic_source_composed_record_count;
  for (int frame = 0; frame < 120; ++frame) {
    interaction.set_subdivision_decrement(true);
    EXPECT_FALSE(submit().dynamic_submitted);
  }
  EXPECT_EQ(interaction.frame().revision, minus_revision);
  EXPECT_EQ(interaction.projection_count(), projections);
  EXPECT_EQ(total_builds, 2u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_composed_record_count,
            composed_records);

  interaction.set_subdivision_decrement(false);
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(target.dynamic_frame.triangles.size(), 3u * kDotTriangles + kButtonTriangles);
  EXPECT_EQ(color_count(target.dynamic_frame, rgba8(0, 0, 0)), 6u);
  EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(total_builds, 3u);
  EXPECT_EQ(target.dynamic_submissions, 3u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, 3u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hash_count, 0u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hashed_record_count, 0u);
}

TEST(PrimitiveCubeOverlayTests, RoundCreationShapesUseOnlyPublishedControlsAndHighlightPoints) {
  for (const auto shape : {ViewportPrimitiveCreationShape::Cone,
                           ViewportPrimitiveCreationShape::Sphere,
                           ViewportPrimitiveCreationShape::Torus}) {
    for (const float scale : {1.0f, 2.0f}) {
      SCOPED_TRACE(::testing::Message() << "shape=" << static_cast<int>(shape)
                                      << " scale=" << scale);
      auto primitive = cylinder_overlay_state();
      primitive.shape = shape;
      ViewportCameraController camera;
      camera.set_viewport_size(800.0f * scale, 600.0f * scale);
      ASSERT_TRUE(camera.restore_perspective_pose(
          {.eye = {8.0f, 7.0f, 11.0f}, .target = {}, .up = {0.0f, 1.0f, 0.0f}}));
      PrimitiveCreationInteraction interaction;
      const auto &controls = interaction.project(primitive, camera, scale);
      const auto overlay_camera =
          ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
      const ViewportToolOverlaySourceInputs inputs{
          .raster_scale = scale,
          .overlay_camera = &overlay_camera,
          .primitive_creation_controls = &controls,
          .interaction_pane = true,
      };
      const std::size_t button_count = shape == ViewportPrimitiveCreationShape::Cone ? 1u : 2u;
      const auto idle = ViewportToolOverlaySource(inputs).build_dynamic_tool_overlay_frame();
      EXPECT_EQ(idle.triangles.size(), 3u * kDotTriangles + button_count * kButtonTriangles);
      EXPECT_TRUE(idle.lines.empty());
      EXPECT_TRUE(controls.label_key.empty());
      for (const int button : {6, 7}) {
        if (button == 7 && button_count == 1u) continue;
        for (const bool decrement : {false, true}) {
          interaction.hover(button);
          interaction.set_subdivision_decrement(decrement);
          ASSERT_EQ(controls.highlight_point_count, 65u);
          EXPECT_FALSE(controls.label_key.empty());
          auto frame = ViewportToolOverlaySource(inputs).build_dynamic_tool_overlay_frame();
          EXPECT_EQ(frame.triangles.size(), 3u * kDotTriangles + button_count *
                    (decrement ? kMinusButtonTriangles : kButtonTriangles));
          EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeFaceColor), 3u * 32u);
          EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeFaceOutlineColor), 3u * 64u);
          EXPECT_EQ(color_count(frame, kOverlayPrimitiveCubeSubdivisionGlyphColor),
                    button_count * (decrement ? 2u : 6u));
          expect_highlight_segments(frame, controls);
          ViewportOverlayStyleResolver::apply(
              frame, ViewportSettings::defaults(), {.raster_scale = scale});
          expect_highlight_segments(frame, controls, scale);
          const auto plan = OverlayRenderableBuilder::build_layer(
              frame, overlay_camera, ViewportOverlayLayer::ToolPreviewLine);
          ASSERT_FALSE(plan.line_batches.empty());
          const auto handles = OverlayRenderableBuilder::build_layer(
              frame, overlay_camera, ViewportOverlayLayer::ToolHandle);
          ASSERT_EQ(handles.face_fill_batches.size(), 1u);
          EXPECT_TRUE(handles.line_batches.empty());
          for (const auto &batch : plan.line_batches) {
            EXPECT_FALSE(batch.state.depth_write);
            EXPECT_FALSE(batch.state.depth_culling);
            EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::Always);
            EXPECT_LT(batch.state.blend_order,
                      handles.face_fill_batches.front().state.blend_order)
                << "Guide loops must never paint over the dots or +/- glyphs";
          }
        }
      }
      interaction.hover(-1);
      const auto cleared = ViewportToolOverlaySource(inputs).build_dynamic_tool_overlay_frame();
      EXPECT_TRUE(cleared.lines.empty());
      EXPECT_TRUE(controls.label_key.empty());
    }
  }
}

TEST(PrimitiveCubeOverlayTests, HighlightOnlyFramesPreserveOpenArcsAndClampPointCount) {
  const auto camera = cube_camera();
  PrimitiveCreationControlsFrame controls;
  controls.visible = true;
  controls.highlight_points[0] = {-1.0f, 0.0f, 0.0f};
  controls.highlight_points[1] = {0.0f, 1.0f, 0.0f};
  controls.highlight_points[2] = {1.0f, 0.0f, 0.0f};
  controls.highlight_point_count = 3u;
  ViewportOverlayFrame frame;
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
  EXPECT_TRUE(frame.triangles.empty());
  expect_highlight_segments(frame, controls);
  ASSERT_EQ(frame.lines.size(), 2u) << "Do not close a published open meridian";
  controls.highlight_point_count = controls.highlight_points.size() + 100u;
  frame = {};
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
  expect_highlight_segments(frame, controls);
  controls.visible = false;
  frame = {};
  ViewportPrimitiveCreationOverlayBuilder::append(frame, camera, controls);
  EXPECT_TRUE(frame.lines.empty());
}

TEST(PrimitiveCubeOverlayTests, SemanticHighlightRevisionGatesCachedOverlayConstruction) {
  const auto camera = cube_camera();
  PrimitiveCreationControlsFrame controls = cube_controls(camera);
  ViewportOverlaySubmissionCoordinator coordinator;
  CubeOverlayTarget target;
  std::uint64_t builds = 0u;
  const auto submit = [&] {
    const CountingCubeSource provider({
        .source_identity = 45u,
        .overlay_camera = &camera,
        .primitive_creation_controls = &controls,
        .interaction_pane = true,
    });
    const auto result = coordinator.submit(
        {.layer_mask = 1u, .camera = camera,
         .dynamic_tool_source = provider.source.source_stamp(),
         .dynamic_tool_frame_provider = &provider},
        ViewportSettings::defaults(), target);
    builds += provider.builds;
    return result;
  };
  EXPECT_TRUE(submit().dynamic_submitted);
  controls.highlight_points[0] = {-1, 0, 0};
  controls.highlight_points[1] = {1, 0, 0};
  controls.highlight_point_count = 2u;
  EXPECT_FALSE(submit().dynamic_submitted) << "Only owner revisions invalidate geometry";
  ++controls.revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  expect_highlight_segments(target.dynamic_frame, controls);
  for (int frame = 0; frame < 120; ++frame) EXPECT_FALSE(submit().dynamic_submitted);
  EXPECT_EQ(builds, 2u);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_hash_count, 0u);
  controls.highlight_point_count = 0u;
  ++controls.revision;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.dynamic_frame.lines.empty());
  EXPECT_EQ(builds, 3u);
}

} // namespace
} // namespace quader

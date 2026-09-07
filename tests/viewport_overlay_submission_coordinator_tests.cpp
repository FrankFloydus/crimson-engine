////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"

#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

float circular_handle_width_pixels(const ViewportOverlayFrame &frame,
                                   const ViewportOverlayCamera &camera) {
  const OverlayCameraProjector projector(camera);
  float min_x = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  for (const auto &triangle : frame.triangles) {
    if (triangle.source != ViewportOverlaySource::Gizmo) continue;
    for (const QVec3 vertex : {triangle.a, triangle.b, triangle.c}) {
      const auto projected = projector.project(projector.view_point_for(vertex));
      min_x = std::min(min_x, projected.pixel.x);
      max_x = std::max(max_x, projected.pixel.x);
    }
  }
  return max_x - min_x;
}

ViewportOverlayLine test_line(ViewportOverlaySource source,
                              float offset = 0.0f) {
  return {
      .start = {offset, 0.0f, 0.0f},
      .end = {offset + 1.0f, 0.0f, 0.0f},
      .source = source,
  };
}

class RecordingOverlayTarget final : public ViewportOverlaySubmissionTarget {
public:
  [[nodiscard]] ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera camera) override {
    ++camera_update_count;
    last_camera = camera;
    return camera_update;
  }

  void submit_persistent_overlay(std::size_t, std::uint8_t,
                                 ViewportOverlayFrame frame,
                                 ViewportOverlayCamera) override {
    ++persistent_submit_count;
    persistent_frame = std::move(frame);
  }

  void submit_dynamic_overlay(std::size_t, std::uint8_t,
                              ViewportOverlayFrame frame,
                              ViewportOverlayCamera) override {
    ++dynamic_submit_count;
    dynamic_frame = std::move(frame);
  }

  ViewportOverlayCameraUpdate camera_update{true, true};
  int camera_update_count = 0;
  int persistent_submit_count = 0;
  int dynamic_submit_count = 0;
  ViewportOverlayCamera last_camera;
  ViewportOverlayFrame persistent_frame;
  ViewportOverlayFrame dynamic_frame;
};

class StaticSelectionFrameProvider final
    : public ViewportOverlaySelectionFrameProvider {
public:
  explicit StaticSelectionFrameProvider(ViewportOverlayFrame frame)
      : frame_(std::move(frame)) {}

  [[nodiscard]] ViewportOverlayFrame
  build_selection_overlay_frame() const override {
    ++build_count;
    return frame_;
  }

  void set_frame(ViewportOverlayFrame frame) { frame_ = std::move(frame); }

  mutable int build_count = 0;

private:
  ViewportOverlayFrame frame_;
};

class StaticDynamicToolFrameProvider final
    : public ViewportOverlayDynamicToolFrameProvider {
public:
  explicit StaticDynamicToolFrameProvider(ViewportOverlayFrame frame)
      : frame_(std::move(frame)) {}

  [[nodiscard]] ViewportOverlayFrame
  build_dynamic_tool_overlay_frame() const override {
    ++build_count;
    return frame_;
  }

  std::uint64_t revision = 1u;
  mutable int build_count = 0;

private:
  ViewportOverlayFrame frame_;
};

const StaticDynamicToolFrameProvider &default_dynamic_tool_provider() {
  static const StaticDynamicToolFrameProvider kProvider = [] {
    ViewportOverlayFrame tool;
    tool.points.push_back({
        .position = {1.0f, 2.0f, 3.0f},
        .source = ViewportOverlaySource::Gizmo,
    });
    return StaticDynamicToolFrameProvider{std::move(tool)};
  }();
  return kProvider;
}

ViewportOverlaySubmissionRequest submission_request(
    const StaticSelectionFrameProvider &selection,
    const RenderOverlaySelectionSnapshot &scene,
    const ViewportOverlayFrame &volatile_modeling,
    const CurveToolPreviewState &curve) {
  const StaticDynamicToolFrameProvider &dynamic_tools =
      default_dynamic_tool_provider();
  return {
      .viewport_index = 1u,
      .layer_mask = 2u,
      .selection_source = {
          .source_identity =
              reinterpret_cast<std::uintptr_t>(&selection),
          .content_revision = 7u,
          .selection_revision = 8u,
          .selection_context = ViewportOverlaySelectionContext::Edge,
      },
      .selection_frame_provider = &selection,
      .scene_overlay_snapshot = &scene,
      .volatile_modeling_overlay_frame = &volatile_modeling,
      .volatile_modeling_source = {
          .source_identity = reinterpret_cast<std::uintptr_t>(&volatile_modeling),
          .publication_revision = 1u,
      },
      .curve_preview = &curve,
      .dynamic_tool_source = {
          .source_identity = reinterpret_cast<std::uintptr_t>(&dynamic_tools),
          .publication_revision = dynamic_tools.revision,
      },
      .dynamic_tool_frame_provider = &dynamic_tools,
      .scene_selection_mode = RenderOverlaySelectionMode::Component,
      .style_selection_context = ViewportOverlaySelectionContext::Edge,
      .overlays_visible = true,
      .scene_overlay_requested = true,
  };
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     ComposesStreamsAndKeepsCurvesOutOfHudAndSourceHash) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();

  ViewportOverlayFrame selection;
  selection.lines.push_back(test_line(ViewportOverlaySource::Selection));
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  scene.scene_revision = 11u;
  scene.tool_overlays.lines.push_back(
      test_line(ViewportOverlaySource::Tool, 2.0f));
  ViewportOverlayFrame volatile_modeling;
  volatile_modeling.lines.push_back(
      test_line(ViewportOverlaySource::Hover, 4.0f));
  CurveToolPreviewState curve;

  const ViewportOverlaySubmissionResult first = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);

  EXPECT_TRUE(first.persistent_submitted);
  EXPECT_TRUE(first.dynamic_submitted);
  EXPECT_TRUE(first.selection_source_refreshed);
  EXPECT_EQ(target.persistent_submit_count, 1);
  EXPECT_EQ(target.dynamic_submit_count, 1);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  EXPECT_EQ(target.persistent_frame.lines.front().source,
            ViewportOverlaySource::Selection);
  ASSERT_NE(first.edge_hud_frame, nullptr);
  EXPECT_EQ(first.edge_hud_frame->lines.size(), 2u);
  EXPECT_EQ(target.dynamic_frame.lines.size(), 2u);
  EXPECT_EQ(first.edge_hud_frame->points.size(), 1u);
  EXPECT_EQ(target.dynamic_frame.points.size(), 1u);

  ViewportOverlaySubmissionRequest camera_changed =
      submission_request(selection_provider, scene, volatile_modeling, curve);
  camera_changed.camera.eye = {9.0f, 8.0f, 7.0f};
  const ViewportOverlaySubmissionResult unchanged = coordinator.submit(
      std::move(camera_changed), settings, target);
  EXPECT_TRUE(unchanged.camera_only);
  EXPECT_FALSE(unchanged.persistent_submitted);
  EXPECT_FALSE(unchanged.dynamic_submitted);
  EXPECT_FALSE(unchanged.selection_source_refreshed);
  EXPECT_EQ(unchanged.dynamic_source_signature,
            first.dynamic_source_signature);
  EXPECT_EQ(target.persistent_submit_count, 1);
  EXPECT_EQ(target.dynamic_submit_count, 1);
  EXPECT_EQ(target.camera_update_count, 1);
  EXPECT_FLOAT_EQ(target.last_camera.eye.x, 9.0f);
  EXPECT_FLOAT_EQ(target.last_camera.eye.y, 8.0f);
  EXPECT_FLOAT_EQ(target.last_camera.eye.z, 7.0f);
  EXPECT_EQ(selection_provider.build_count, 1);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     SelectedPointLightRadiusReusesSceneGeometryAndBroadSourcesDuringNavigation) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  StaticSelectionFrameProvider selection_provider{ViewportOverlayFrame{}};
  RenderOverlaySelectionSnapshot scene;
  scene.scene_revision = 1u;
  scene.point_light_gizmos.push_back({
      .entity = 17u, .type = LightType::Point, .radius = 5.0f,
      .selected = true, .radius_editable = true});
  for (std::size_t index = 0u; index < 128u; ++index) {
    scene.point_light_gizmos.push_back({
        .entity = static_cast<EntityId>(100u + index), .type = LightType::Point});
  }
  ViewportOverlayFrame volatile_modeling;
  for (std::size_t index = 0u; index < 8192u; ++index) {
    volatile_modeling.lines.push_back(
        test_line(ViewportOverlaySource::Tool, static_cast<float>(index)));
  }
  CurveToolPreviewState curve;
  auto request = submission_request(selection_provider, scene, volatile_modeling, curve);
  request.point_light_gizmos_visible = true;
  request.camera.eye = {10.0f, 0.0f, 10.0f};
  request.camera.viewport_size = {800.0f, 600.0f};
  const auto initial = coordinator.submit(request, settings, target);
  ASSERT_EQ(target.persistent_frame.lines.size(), 360u);
  ASSERT_EQ(target.persistent_frame.icons.size(), 129u);
  ASSERT_EQ(target.dynamic_frame.lines.size(), 8192u + 120u);
  ASSERT_EQ(target.dynamic_frame.triangles.size(), 96u);
  ASSERT_EQ(target.dynamic_frame.points.size(), 1u);
  EXPECT_FLOAT_EQ(target.dynamic_frame.triangles.front().a.x, 5.0f);
  EXPECT_NEAR(circular_handle_width_pixels(target.dynamic_frame, request.camera),
              12.0f, 0.001f);
  EXPECT_EQ(initial.edge_hud_frame->lines.size(), 8192u);
  EXPECT_EQ(initial.edge_hud_frame->points.size(), 1u);
  const auto initial_work = coordinator.work_counters();
  EXPECT_EQ(initial_work.point_light_radius_source_refresh_count, 1u);
  EXPECT_EQ(initial_work.point_light_radius_camera_build_count, 1u);

  for (int frame = 0; frame < 3; ++frame) {
    const auto idle = coordinator.submit(request, settings, target);
    EXPECT_TRUE(idle.camera_only);
    EXPECT_EQ(idle.dynamic_source_signature, initial.dynamic_source_signature);
  }
  request.camera.right = {0.0f, 0.0f, -1.0f};
  request.camera.forward = {-1.0f, 0.0f, 0.0f};
  const auto rotated = coordinator.submit(request, settings, target);
  EXPECT_FALSE(rotated.persistent_submitted);
  EXPECT_TRUE(rotated.dynamic_submitted);
  EXPECT_EQ(rotated.dynamic_source_signature, initial.dynamic_source_signature);
  ASSERT_EQ(target.dynamic_frame.triangles.size(), 96u);
  ASSERT_EQ(target.dynamic_frame.points.size(), 1u);
  EXPECT_FLOAT_EQ(target.dynamic_frame.triangles.front().a.x, 0.0f);
  EXPECT_FLOAT_EQ(target.dynamic_frame.triangles.front().a.z, -5.0f);
  EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  const auto rotated_work = coordinator.work_counters();
  EXPECT_EQ(rotated_work.point_light_radius_source_refresh_count, 1u);
  EXPECT_EQ(rotated_work.point_light_radius_camera_build_count, 2u);
  EXPECT_EQ(rotated_work.dynamic_source_compose_count,
            initial_work.dynamic_source_compose_count);
  EXPECT_EQ(rotated_work.dynamic_source_composed_record_count,
            initial_work.dynamic_source_composed_record_count);
  EXPECT_EQ(rotated_work.dynamic_source_hash_count, 0u);
  EXPECT_EQ(target.persistent_submit_count, 1);

  request.raster_scale = 2.0f;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);
  EXPECT_NEAR(circular_handle_width_pixels(target.dynamic_frame, request.camera),
              24.0f, 0.001f);
  const auto radius_line = std::find_if(target.dynamic_frame.lines.begin(),
                                        target.dynamic_frame.lines.end(),
      [](const auto &line) { return line.source == ViewportOverlaySource::Gizmo; });
  ASSERT_NE(radius_line, target.dynamic_frame.lines.end());
  EXPECT_FLOAT_EQ(radius_line->width_pixels, 2.0f);
  EXPECT_EQ(coordinator.work_counters().point_light_radius_camera_build_count, 3u);

  const auto scaled_work = coordinator.work_counters();
  for (int change = 0; change < 4; ++change) {
    if (change == 0) request.camera.eye.x = 20.0f;
    if (change == 1) request.camera.fov_degrees = 35.0f;
    if (change == 2) request.camera.viewport_size.y = 900.0f;
    if (change == 3) {
      request.camera.projection = ViewportOverlayProjection::Orthographic;
      request.camera.orthographic_size = 18.0f;
    }
    const auto projected = coordinator.submit(request, settings, target);
    EXPECT_TRUE(projected.dynamic_submitted);
    EXPECT_NEAR(circular_handle_width_pixels(target.dynamic_frame, request.camera),
                24.0f, 0.001f);
    EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  }
  EXPECT_EQ(coordinator.work_counters().point_light_radius_camera_build_count, 7u);
  EXPECT_EQ(coordinator.work_counters().point_light_radius_source_refresh_count,
            scaled_work.point_light_radius_source_refresh_count);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            scaled_work.dynamic_source_compose_count);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     PointLightRadiusUpdatesRangeTransformSelectionAndVisibility) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  StaticSelectionFrameProvider selection_provider{ViewportOverlayFrame{}};
  RenderOverlaySelectionSnapshot scene;
  scene.scene_revision = 1u;
  scene.point_light_gizmos.push_back({
      .entity = 17u, .type = LightType::Point, .radius = 5.0f,
      .selected = true, .radius_editable = true});
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;
  auto request = submission_request(selection_provider, scene, volatile_modeling, curve);
  request.point_light_gizmos_visible = true;
  request.camera.eye = {0.0f, 0.0f, 10.0f};
  request.camera.viewport_size = {800.0f, 600.0f};
  (void)coordinator.submit(request, settings, target);
  scene.point_light_gizmos.front().radius = 8.0f;
  ++scene.render_component_revision;
  ++scene.scene_revision;
  const auto resized = coordinator.submit(request, settings, target);
  EXPECT_TRUE(resized.persistent_submitted);
  EXPECT_TRUE(resized.dynamic_submitted);
  EXPECT_FLOAT_EQ(target.persistent_frame.lines.front().start.x, 8.0f);
  ASSERT_EQ(target.dynamic_frame.triangles.size(), 96u);
  EXPECT_FLOAT_EQ(target.dynamic_frame.triangles.front().a.x, 8.0f);

  scene.point_light_gizmos.front().world_transform.values[12] = 3.0f;
  ++scene.transform_revision;
  const auto moved = coordinator.submit(request, settings, target);
  EXPECT_TRUE(moved.persistent_submitted);
  EXPECT_TRUE(moved.dynamic_submitted);
  EXPECT_EQ(moved.dynamic_source_signature, resized.dynamic_source_signature);
  ASSERT_EQ(target.dynamic_frame.triangles.size(), 96u);
  EXPECT_FLOAT_EQ(target.dynamic_frame.triangles.front().a.x, 11.0f);

  scene.point_light_gizmos.front().radius_editable = false;
  ++scene.scene_revision;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);
  EXPECT_EQ(target.dynamic_frame.lines.size(), 120u);
  EXPECT_EQ(target.dynamic_frame.points.size(), 1u);
  EXPECT_TRUE(target.dynamic_frame.triangles.empty());

  for (bool overlays_visible : {false, true}) {
    request.overlays_visible = overlays_visible;
    request.point_light_gizmos_visible = !overlays_visible;
    (void)coordinator.submit(request, settings, target);
    EXPECT_TRUE(target.persistent_frame.lines.empty());
    EXPECT_TRUE(target.dynamic_frame.lines.empty());
    EXPECT_EQ(target.dynamic_frame.points.size(), 1u);
    EXPECT_TRUE(target.dynamic_frame.triangles.empty());
  }
  request.point_light_gizmos_visible = true;
  scene.point_light_gizmos.front().selected = false;
  ++scene.selection_revision;
  (void)coordinator.submit(request, settings, target);
  EXPECT_TRUE(target.persistent_frame.lines.empty());
  EXPECT_TRUE(target.dynamic_frame.lines.empty());
  const auto unselected_work = coordinator.work_counters();
  request.camera.right = {0.0f, 0.0f, -1.0f};
  request.camera.forward = {-1.0f, 0.0f, 0.0f};
  EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  EXPECT_EQ(coordinator.work_counters().point_light_radius_source_refresh_count,
            unselected_work.point_light_radius_source_refresh_count);
  EXPECT_EQ(coordinator.work_counters().point_light_radius_camera_build_count,
            unselected_work.point_light_radius_camera_build_count);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     SourceAndStyleChangesInvalidateOwnedPublications) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  ViewportSettings settings = ViewportSettings::defaults();

  ViewportOverlayFrame selection;
  selection.lines.push_back(test_line(ViewportOverlaySource::Selection));
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;

  (void)coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  const std::uint32_t initial_styled_color =
      target.persistent_frame.lines.front().color;

  ViewportOverlaySubmissionRequest source_changed =
      submission_request(selection_provider, scene, volatile_modeling, curve);
  ++source_changed.selection_source.selection_revision;
  const ViewportOverlaySubmissionResult source_result = coordinator.submit(
      std::move(source_changed), settings, target);
  EXPECT_TRUE(source_result.selection_source_refreshed);
  EXPECT_TRUE(source_result.persistent_submitted);
  EXPECT_FALSE(source_result.dynamic_submitted);
  EXPECT_EQ(selection_provider.build_count, 2);

  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SourceWire,
                                 {0.15f, 0.35f, 0.75f, 1.0f}));
  ViewportOverlaySubmissionRequest style_changed =
      submission_request(selection_provider, scene, volatile_modeling, curve);
  ++style_changed.selection_source.selection_revision;
  const ViewportOverlaySubmissionResult style_result = coordinator.submit(
      std::move(style_changed), settings, target);
  EXPECT_FALSE(style_result.selection_source_refreshed);
  EXPECT_TRUE(style_result.persistent_submitted);
  EXPECT_TRUE(style_result.dynamic_submitted);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  EXPECT_NE(target.persistent_frame.lines.front().color,
            initial_styled_color);
  EXPECT_EQ(selection_provider.build_count, 2);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     OpaqueComponentSelectionReplacesItsSourceWireAndPreservesUserStyle) {
  for (const auto layer : {ViewportOverlayLayer::SelectedEdge,
                           ViewportOverlayLayer::SelectedFaceEdge}) {
    SCOPED_TRACE(static_cast<int>(layer));
    ViewportOverlaySubmissionCoordinator coordinator;
    RecordingOverlayTarget target;
    ViewportSettings settings = ViewportSettings::defaults();
    (void)settings.set_float_value(ViewportFloatSettingId::SelectionEdgeLineSize,
                                   1.0f);
    (void)settings.set_float_value(
        ViewportFloatSettingId::SelectionFaceWireLineSize, 2.0f);
    (void)settings.set_float_value(ViewportFloatSettingId::SourceWireLineSize,
                                   1.5f);

    auto source = test_line(ViewportOverlaySource::ComponentSelection);
    source.element = {.object_id = 7, .edge_a = 3, .edge_b = 9};
    auto selected = source;
    selected.layer = layer;
    std::swap(selected.start, selected.end);
    std::swap(selected.element.edge_a, selected.element.edge_b);
    auto unselected = test_line(ViewportOverlaySource::ComponentSelection, 2.0f);
    unselected.element = {.object_id = 7, .edge_a = 9, .edge_b = 12};
    ViewportOverlayFrame selection;
    selection.lines = {source, selected, unselected};
    StaticSelectionFrameProvider provider{selection};
    RenderOverlaySelectionSnapshot scene;
    ViewportOverlayFrame volatile_modeling;
    CurveToolPreviewState curve;
    auto request = submission_request(provider, scene, volatile_modeling, curve);
    request.selection_source.selection_context =
        layer == ViewportOverlayLayer::SelectedEdge
            ? ViewportOverlaySelectionContext::Edge
            : ViewportOverlaySelectionContext::Face;
    request.style_selection_context = request.selection_source.selection_context;

    EXPECT_TRUE(coordinator.submit(request, settings, target).persistent_submitted);
    ASSERT_EQ(target.persistent_frame.lines.size(), 2u);
    EXPECT_EQ(target.persistent_frame.lines[0].layer, layer);
    EXPECT_EQ(target.persistent_frame.lines[0].color, rgba8(255, 211, 31));
    EXPECT_FLOAT_EQ(target.persistent_frame.lines[0].width_pixels,
                    layer == ViewportOverlayLayer::SelectedEdge ? 1.0f : 2.0f);
    EXPECT_EQ(target.persistent_frame.lines[1].layer, ViewportOverlayLayer::SourceWire);
    EXPECT_EQ(target.persistent_frame.lines[1].element.edge_b, 12);
    EXPECT_FLOAT_EQ(target.persistent_frame.lines[1].width_pixels, 1.5f);
    const auto source_color = target.persistent_frame.lines[1].color;

    ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire,
                                   {0.2f, 0.4f, 0.6f, 1.0f}));
    const auto restyled = coordinator.submit(request, settings, target);
    EXPECT_TRUE(restyled.persistent_submitted);
    EXPECT_FALSE(restyled.selection_source_refreshed);
    ASSERT_EQ(target.persistent_frame.lines.size(), 2u);
    EXPECT_EQ(target.persistent_frame.lines[0].color, rgba8(51, 102, 153));
    EXPECT_EQ(target.persistent_frame.lines[1].color, source_color);

    // A translucent or hidden highlight still needs its ordinary source wire.
    // Restyling the cached semantic frame must recover that wire without rebuilding it.
    for (const float alpha : {0.5f, 0.0f}) {
      ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire,
                                     {0.2f, 0.4f, 0.6f, alpha}));
      const auto translucent = coordinator.submit(request, settings, target);
      EXPECT_TRUE(translucent.persistent_submitted);
      EXPECT_FALSE(translucent.selection_source_refreshed);
      ASSERT_EQ(target.persistent_frame.lines.size(), 3u);
      EXPECT_EQ(target.persistent_frame.lines[0].layer, ViewportOverlayLayer::SourceWire);
      EXPECT_EQ(target.persistent_frame.lines[0].color, source_color);
    }
    ASSERT_TRUE(settings.set_color(ViewportColorSettingId::SelectionWire,
                                   {0.2f, 0.4f, 0.6f, 1.0f}));
    EXPECT_TRUE(coordinator.submit(request, settings, target).persistent_submitted);
    ASSERT_EQ(target.persistent_frame.lines.size(), 2u);
    EXPECT_EQ(provider.build_count, 1);

    const int submissions_before = target.persistent_submit_count;
    const int dynamic_submissions_before = target.dynamic_submit_count;
    const auto work_before = coordinator.work_counters();
    request.camera.eye = {4.0f, 5.0f, 6.0f};
    const auto camera_only = coordinator.submit(request, settings, target);
    EXPECT_TRUE(camera_only.camera_only);
    EXPECT_FALSE(camera_only.selection_source_refreshed);
    EXPECT_FALSE(camera_only.persistent_submitted);
    EXPECT_FALSE(camera_only.dynamic_submitted);
    EXPECT_EQ(target.persistent_submit_count, submissions_before);
    EXPECT_EQ(target.dynamic_submit_count, dynamic_submissions_before);
    EXPECT_EQ(provider.build_count, 1);
    EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
              work_before.dynamic_source_compose_count);

    selection.lines = {source, unselected};
    provider.set_frame(std::move(selection));
    ++request.selection_source.selection_revision;
    const auto deselected = coordinator.submit(request, settings, target);
    EXPECT_TRUE(deselected.selection_source_refreshed);
    EXPECT_TRUE(deselected.persistent_submitted);
    EXPECT_FALSE(deselected.dynamic_submitted);
    EXPECT_EQ(provider.build_count, 2);
    ASSERT_EQ(target.persistent_frame.lines.size(), 2u);
    for (const auto &line : target.persistent_frame.lines) {
      EXPECT_EQ(line.layer, ViewportOverlayLayer::SourceWire);
      EXPECT_EQ(line.color, source_color);
      EXPECT_FLOAT_EQ(line.width_pixels, 1.5f);
    }
  }
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     SelectedWireReplacementRequiresTheSameKnownComponentEdgeOccurrence) {
  for (const auto layer : {ViewportOverlayLayer::SelectedEdge,
                           ViewportOverlayLayer::SelectedFaceEdge}) {
    SCOPED_TRACE(static_cast<int>(layer));
    auto source = test_line(ViewportOverlaySource::ComponentSelection);
    source.element = {.object_id = 7, .edge_a = 3, .edge_b = 9};
    auto selected = source;
    selected.layer = layer;
    ViewportOverlayFrame selection;
    selection.lines = {selected, source};

    auto other_object = source;
    other_object.element.object_id = 8;
    selection.lines.push_back(other_object);
    auto coincident_edge = source;
    coincident_edge.element.edge_b = 10;
    selection.lines.push_back(coincident_edge);
    auto other_occurrence = source;
    other_occurrence.start.y = 2.0f;
    other_occurrence.end.y = 2.0f;
    selection.lines.push_back(other_occurrence);
    auto nearby_endpoint = source;
    nearby_endpoint.end.x += 0.0001f;
    selection.lines.push_back(nearby_endpoint);
    auto external_source = source;
    external_source.source = ViewportOverlaySource::SceneWireframe;
    selection.lines.push_back(external_source);

    // Incomplete or degenerate identities cannot establish ownership, even when
    // the selected line carries an identical reference and world-space segment.
    for (const auto element : {
             ViewportOverlayElementRef{.object_id = -1, .edge_a = 3, .edge_b = 9},
             ViewportOverlayElementRef{.object_id = 7, .edge_a = -1, .edge_b = 9},
             ViewportOverlayElementRef{.object_id = 7, .edge_a = 3, .edge_b = 3}}) {
      auto unknown_source = source;
      unknown_source.element = element;
      selection.lines.push_back(unknown_source);
      unknown_source.layer = layer;
      selection.lines.push_back(unknown_source);
    }
    auto external_selected = source;
    external_selected.element.edge_b = 14;
    selection.lines.push_back(external_selected);
    external_selected.layer = layer;
    external_selected.source = ViewportOverlaySource::Selection;
    selection.lines.push_back(external_selected);

    const auto authored_lines = selection.lines;
    StaticSelectionFrameProvider provider{std::move(selection)};
    RenderOverlaySelectionSnapshot scene;
    ViewportOverlayFrame volatile_modeling;
    CurveToolPreviewState curve;
    ViewportOverlaySubmissionCoordinator coordinator;
    RecordingOverlayTarget target;
    EXPECT_TRUE(coordinator.submit(
        submission_request(provider, scene, volatile_modeling, curve),
        ViewportSettings::defaults(), target).persistent_submitted);
    ASSERT_EQ(target.persistent_frame.lines.size(), authored_lines.size() - 1u);
    EXPECT_EQ(target.persistent_frame.lines.front().layer, layer);
    for (std::size_t i = 1; i < target.persistent_frame.lines.size(); ++i) {
      const auto &line = target.persistent_frame.lines[i];
      const auto &authored = authored_lines[i + 1u];
      EXPECT_EQ(line.layer, authored.layer);
      EXPECT_EQ(line.source, authored.source);
      EXPECT_EQ(line.element.object_id, authored.element.object_id);
      EXPECT_EQ(line.element.edge_a, authored.element.edge_a);
      EXPECT_EQ(line.element.edge_b, authored.element.edge_b);
      EXPECT_FLOAT_EQ(line.start.y, authored.start.y);
      EXPECT_FLOAT_EQ(line.end.x, authored.end.x);
    }
  }
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     RasterScaleRestylesBothStreamsAndRebuildsDynamicTopologySource) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();

  ViewportOverlayFrame selection;
  selection.lines.push_back(test_line(ViewportOverlaySource::Selection));
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;
  const int dynamic_builds_before =
      default_dynamic_tool_provider().build_count;

  const ViewportOverlaySubmissionResult initial = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  ASSERT_TRUE(initial.persistent_submitted);
  ASSERT_TRUE(initial.dynamic_submitted);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  ASSERT_EQ(target.dynamic_frame.points.size(), 1u);
  const float initial_line_width =
      target.persistent_frame.lines.front().width_pixels;
  const float initial_point_size =
      target.dynamic_frame.points.front().size_pixels;
  const std::uint64_t initial_source_revision =
      initial.dynamic_source_signature;

  ViewportOverlaySubmissionRequest invalid =
      submission_request(selection_provider, scene, volatile_modeling, curve);
  invalid.raster_scale = std::numeric_limits<float>::quiet_NaN();
  const ViewportOverlaySubmissionResult sanitized =
      coordinator.submit(std::move(invalid), settings, target);
  EXPECT_TRUE(sanitized.camera_only);

  ViewportOverlaySubmissionRequest scaled =
      submission_request(selection_provider, scene, volatile_modeling, curve);
  scaled.raster_scale = 2.0f;
  const ViewportOverlaySubmissionResult scaled_result =
      coordinator.submit(std::move(scaled), settings, target);
  EXPECT_FALSE(scaled_result.selection_source_refreshed);
  EXPECT_TRUE(scaled_result.persistent_submitted);
  EXPECT_TRUE(scaled_result.dynamic_submitted);
  EXPECT_NE(scaled_result.dynamic_source_signature, initial_source_revision);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  ASSERT_EQ(target.dynamic_frame.points.size(), 1u);
  EXPECT_FLOAT_EQ(target.persistent_frame.lines.front().width_pixels,
                  initial_line_width * 2.0f);
  EXPECT_FLOAT_EQ(target.dynamic_frame.points.front().size_pixels,
                  initial_point_size * 2.0f);
  EXPECT_EQ(default_dynamic_tool_provider().build_count,
            dynamic_builds_before + 2);
  EXPECT_EQ(selection_provider.build_count, 1);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     MixedDpiPanesShareSemanticSelectionSourceAndReachFastPath) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();

  ViewportOverlayFrame selection;
  selection.lines.push_back(test_line(ViewportOverlaySource::Selection));
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;

  auto submit_pane = [&](std::size_t pane, float raster_scale) {
    ViewportOverlaySubmissionRequest request = submission_request(
        selection_provider, scene, volatile_modeling, curve);
    request.viewport_index = pane;
    request.raster_scale = raster_scale;
    return coordinator.submit(std::move(request), settings, target);
  };

  const ViewportOverlaySubmissionResult pane_1x = submit_pane(0u, 1.0f);
  ASSERT_TRUE(pane_1x.persistent_submitted);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  const float width_1x = target.persistent_frame.lines.front().width_pixels;

  const ViewportOverlaySubmissionResult pane_2x = submit_pane(1u, 2.0f);
  ASSERT_TRUE(pane_2x.persistent_submitted);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  EXPECT_FLOAT_EQ(target.persistent_frame.lines.front().width_pixels,
                  width_1x * 2.0f);
  EXPECT_EQ(selection_provider.build_count, 1);

  const int persistent_submits = target.persistent_submit_count;
  const int dynamic_submits = target.dynamic_submit_count;
  EXPECT_TRUE(submit_pane(0u, 1.0f).camera_only);
  EXPECT_TRUE(submit_pane(1u, 2.0f).camera_only);
  EXPECT_EQ(selection_provider.build_count, 1);
  EXPECT_EQ(target.persistent_submit_count, persistent_submits);
  EXPECT_EQ(target.dynamic_submit_count, dynamic_submits);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     CurvePublicationRevisionSurvivesCacheReset) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  ViewportOverlayFrame selection;
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;

  (void)coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  const auto first = coordinator.work_counters();
  ASSERT_EQ(first.curve_overlay_build_count, 1u);
  coordinator.invalidate_all();
  (void)coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  EXPECT_EQ(coordinator.work_counters().curve_overlay_build_count, 2u);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     RecoversOnlyTheBackendStreamLostDuringRebind) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  ViewportOverlayFrame selection;
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;

  (void)coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  target.camera_update = {
      .persistent_available = true,
      .dynamic_available = false,
  };

  const ViewportOverlaySubmissionResult recovered = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);

  EXPECT_FALSE(recovered.persistent_recovered);
  EXPECT_TRUE(recovered.dynamic_recovered);
  EXPECT_FALSE(recovered.persistent_submitted);
  EXPECT_TRUE(recovered.dynamic_submitted);
  EXPECT_EQ(target.persistent_submit_count, 1);
  EXPECT_EQ(target.dynamic_submit_count, 2);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     InvalidatesPersistentAndDynamicStreamsFromIndependentTypedStamps) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  ViewportOverlayFrame selection;
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;

  (void)coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  scene.transform_revision = 2u;
  const ViewportOverlaySubmissionResult scene_changed = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  EXPECT_TRUE(scene_changed.persistent_submitted);
  EXPECT_FALSE(scene_changed.dynamic_submitted);

  ++curve.revision;
  const ViewportOverlaySubmissionResult curve_changed = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  EXPECT_FALSE(curve_changed.persistent_submitted);
  EXPECT_TRUE(curve_changed.dynamic_submitted);

  coordinator.set_active_viewports(0u);
  const ViewportOverlaySubmissionResult reactivated = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  EXPECT_TRUE(reactivated.persistent_submitted);
  EXPECT_TRUE(reactivated.dynamic_submitted);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     BuildsDynamicProviderOnlyAfterItsTypedPublicationChanges) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  ViewportOverlayFrame selection;
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;
  const int builds_before = default_dynamic_tool_provider().build_count;

  (void)coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  ASSERT_EQ(default_dynamic_tool_provider().build_count, builds_before + 1);
  const ViewportOverlaySubmissionWorkCounters first =
      coordinator.work_counters();

  const ViewportOverlaySubmissionResult unchanged = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  EXPECT_TRUE(unchanged.camera_only);
  EXPECT_EQ(default_dynamic_tool_provider().build_count, builds_before + 1);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            first.dynamic_source_compose_count);

  ViewportOverlaySubmissionRequest changed =
      submission_request(selection_provider, scene, volatile_modeling, curve);
  ++changed.dynamic_tool_source.publication_revision;
  const ViewportOverlaySubmissionResult changed_result = coordinator.submit(
      std::move(changed), settings, target);
  EXPECT_TRUE(changed_result.dynamic_submitted);
  EXPECT_EQ(default_dynamic_tool_provider().build_count, builds_before + 2);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            first.dynamic_source_compose_count + 1u);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     HoverEdgeDimensionContextFollowsTheVolatilePublicationRevision) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  ViewportOverlayFrame selection;
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  volatile_modeling.lines.push_back(
      test_line(ViewportOverlaySource::Hover, 4.0F));
  volatile_modeling.hover_edge_dimension_context =
      ViewportHoverEdgeDimensionContext{
          .inside_reference = {4.5F, 0.5F, 0.0F},
          .element = {
              .object_id = 17,
              .edge_a = 3,
              .edge_b = 4,
              .face_id = 8,
          },
      };
  CurveToolPreviewState curve;

  const ViewportOverlaySubmissionResult first = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  ASSERT_TRUE(first.dynamic_submitted);
  ASSERT_NE(first.edge_hud_frame, nullptr);
  ASSERT_TRUE(first.edge_hud_frame->hover_edge_dimension_context.has_value());
  ASSERT_TRUE(target.dynamic_frame.hover_edge_dimension_context.has_value());
  EXPECT_FLOAT_EQ(first.edge_hud_frame->hover_edge_dimension_context
                      ->inside_reference.y,
                  0.5F);
  EXPECT_EQ(target.dynamic_frame.hover_edge_dimension_context->element.face_id,
            8);
  const std::uint64_t first_source_signature = first.dynamic_source_signature;
  const auto first_work = coordinator.work_counters();

  volatile_modeling.hover_edge_dimension_context->inside_reference.y = 0.75F;
  const ViewportOverlaySubmissionResult stale = coordinator.submit(
      submission_request(selection_provider, scene, volatile_modeling, curve),
      settings, target);
  EXPECT_TRUE(stale.camera_only);
  EXPECT_FALSE(stale.dynamic_submitted);
  EXPECT_EQ(stale.dynamic_source_signature, first_source_signature);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            first_work.dynamic_source_compose_count);
  EXPECT_FLOAT_EQ(target.dynamic_frame.hover_edge_dimension_context
                      ->inside_reference.y,
                  0.5F);

  ViewportOverlaySubmissionRequest changed =
      submission_request(selection_provider, scene, volatile_modeling, curve);
  ++changed.volatile_modeling_source.publication_revision;
  const ViewportOverlaySubmissionResult updated =
      coordinator.submit(std::move(changed), settings, target);
  EXPECT_TRUE(updated.dynamic_submitted);
  EXPECT_NE(updated.dynamic_source_signature, first_source_signature);
  ASSERT_NE(updated.edge_hud_frame, nullptr);
  ASSERT_TRUE(
      updated.edge_hud_frame->hover_edge_dimension_context.has_value());
  EXPECT_FLOAT_EQ(updated.edge_hud_frame->hover_edge_dimension_context
                      ->inside_reference.y,
                  0.75F);
  ASSERT_TRUE(target.dynamic_frame.hover_edge_dimension_context.has_value());
  EXPECT_FLOAT_EQ(target.dynamic_frame.hover_edge_dimension_context
                      ->inside_reference.y,
                  0.75F);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            first_work.dynamic_source_compose_count + 1U);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     UnchangedMultiPaneCutKnifeAndCurveSourcesDoNoBroadWork) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const ViewportSettings settings = ViewportSettings::defaults();
  ViewportOverlayFrame selection;
  StaticSelectionFrameProvider selection_provider{std::move(selection)};
  RenderOverlaySelectionSnapshot scene;

  // Stand in for the large projected point/segment/fill streams produced by
  // active Knife and Cut tools.
  ViewportOverlayFrame volatile_modeling;
  constexpr std::size_t kVolatileRecordCount = 8192u;
  volatile_modeling.lines.reserve(kVolatileRecordCount);
  volatile_modeling.points.reserve(kVolatileRecordCount);
  for (std::size_t index = 0u; index < kVolatileRecordCount; ++index) {
    volatile_modeling.lines.push_back(
        test_line(ViewportOverlaySource::Tool, static_cast<float>(index)));
    volatile_modeling.points.push_back({
        .position = {static_cast<float>(index), 0.0f, 0.0f},
        .source = ViewportOverlaySource::Tool,
    });
  }

  std::vector<modeling::CurvePointSpec> authored_points;
  authored_points.reserve(512u);
  for (std::size_t index = 0u; index < 512u; ++index) {
    authored_points.push_back({
        .position = {static_cast<float>(index), 0.0f, 0.0f},
        .incoming_handle = {-0.25f, 0.0f, 0.0f},
        .outgoing_handle = {0.25f, 0.0f, 0.0f},
    });
  }
  CurveToolPreviewState curve;
  curve.active = true;
  curve.drawing = true;
  curve.document =
      modeling::CurveDocument::make_bezier(authored_points).or_throw();
  curve.revision = 7u;
  const int dynamic_provider_builds_before =
      default_dynamic_tool_provider().build_count;

  for (std::size_t pane = 0u;
       pane < ViewportOverlaySubmissionCoordinator::kMaxViewportSlots;
       ++pane) {
    auto request =
        submission_request(selection_provider, scene, volatile_modeling, curve);
    request.viewport_index = pane;
    (void)coordinator.submit(std::move(request), settings, target);
  }
  const ViewportOverlaySubmissionWorkCounters first =
      coordinator.work_counters();
  ASSERT_EQ(first.curve_overlay_build_count, 1u);
  ASSERT_EQ(first.dynamic_source_compose_count, 4u);
  ASSERT_EQ(first.dynamic_source_hash_count, 0u);
  ASSERT_EQ(first.dynamic_source_hashed_record_count, 0u);
  ASSERT_EQ(default_dynamic_tool_provider().build_count,
            dynamic_provider_builds_before + 4);
  ASSERT_EQ(target.dynamic_submit_count, 4);

  for (std::size_t pane = 0u;
       pane < ViewportOverlaySubmissionCoordinator::kMaxViewportSlots;
       ++pane) {
    auto request =
        submission_request(selection_provider, scene, volatile_modeling, curve);
    request.viewport_index = pane;
    const auto unchanged =
        coordinator.submit(std::move(request), settings, target);
    EXPECT_TRUE(unchanged.camera_only);
  }
  const ViewportOverlaySubmissionWorkCounters unchanged =
      coordinator.work_counters();
  EXPECT_EQ(unchanged.curve_overlay_build_count,
            first.curve_overlay_build_count);
  EXPECT_EQ(unchanged.dynamic_source_compose_count,
            first.dynamic_source_compose_count);
  EXPECT_EQ(unchanged.dynamic_source_composed_record_count,
            first.dynamic_source_composed_record_count);
  EXPECT_EQ(unchanged.dynamic_source_hash_count,
            first.dynamic_source_hash_count);
  EXPECT_EQ(unchanged.dynamic_source_hashed_record_count,
            first.dynamic_source_hashed_record_count);
  EXPECT_EQ(default_dynamic_tool_provider().build_count,
            dynamic_provider_builds_before + 4);
  EXPECT_EQ(target.dynamic_submit_count, 4);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     LivePrimitiveHasFaintTopologyWithoutFillAndRestoresAfterCreation) {
  for (const std::string_view kind : {"box", "cylinder", "cone", "sphere", "torus"}) {
    SCOPED_TRACE(kind);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId,
                                          kPrimitiveTypeSetting, kind).ok());
    ModelingBoxPreview preview;
    preview.ok = preview.valid = true;
    preview.drag_state = ModelingBoxDragState::Height;
    preview.corners = {QVec3{-1, 0, -1}, QVec3{-1, 0, 1}, QVec3{1, 0, 1},
                       QVec3{1, 0, -1}, QVec3{-1, 2, -1}, QVec3{-1, 2, 1},
                       QVec3{1, 2, 1}, QVec3{1, 2, -1}};
    preview.corner_count = preview.corners.size();
    ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Wire preview").ok);
    const auto live = editor.tools().live_primitive_creation();
    ASSERT_TRUE(live.has_value());

    // This is the actual Qt selection provider's input, including the mesh
    // edges that survive suppression of the separate selected-object fill.
    const ViewportOverlayFrame topology = session.modeling().selection_overlay_frame(
        {.suppress_persistent_object_selection = true});
    ASSERT_FALSE(topology.lines.empty());
    ViewportOverlayFrame selection_with_fill = topology;
    const auto full_selection = session.modeling().selection_overlay_frame();
    ASSERT_FALSE(full_selection.triangles.empty());
    selection_with_fill.triangles = full_selection.triangles;
    StaticSelectionFrameProvider selection(selection_with_fill);
    const auto scene = build_render_overlay_selection_snapshot(session.scene(), session.assets());
    ViewportOverlayFrame hover;
    hover.lines.push_back(topology.lines.front());
    hover.lines.back().source = ViewportOverlaySource::Hover;
    hover.lines.back().style_role = ViewportOverlayStyleRole::HoverWire;
    hover.lines.push_back(hover.lines.front());
    hover.lines.back().element.object_id += 1000;
    hover.triangles.push_back({
        .layer = ViewportOverlayLayer::ToolHandle,
        .a = {-1, 0, 0}, .b = {1, 0, 0}, .c = {0, 1, 0},
        .color = kOverlayPrimitiveCubeFaceColor,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Tool,
        .element = {.object_id = live->object_id},
    });
    hover.triangles.push_back(full_selection.triangles.front());
    hover.triangles.back().layer = ViewportOverlayLayer::HoverFace;
    hover.triangles.back().style_role = ViewportOverlayStyleRole::HoverFace;
    hover.triangles.back().source = ViewportOverlaySource::Hover;
    hover.triangles.push_back(hover.triangles.back());
    hover.triangles.back().element.object_id += 1000;
    CurveToolPreviewState curve;
    auto request = submission_request(selection, scene, hover, curve);
    request.scene_selection_mode = RenderOverlaySelectionMode::Object;
    request.style_selection_context = ViewportOverlaySelectionContext::Object;
    request.selection_source.selection_context = ViewportOverlaySelectionContext::Object;
    ViewportOverlaySubmissionCoordinator coordinator;
    RecordingOverlayTarget target;
    const auto settings = ViewportSettings::defaults();
    (void)coordinator.submit(request, settings, target);
    const auto ordinary = target.persistent_frame;
    const auto ordinary_hover = target.dynamic_frame;
    ASSERT_EQ(ordinary.lines.size(), topology.lines.size() + 12u);
    ASSERT_FALSE(ordinary.triangles.empty());
    ASSERT_EQ(ordinary_hover.triangles.size(), 3u);

    RenderOverlaySnapshotCache native_cache;
    const auto native_selected = native_cache.update_persistent(
        session.scene(), session.assets(), true, 0u, {});
    EXPECT_FALSE(native_selected.selected_object->meshes.empty());
    const auto native_creation = native_cache.update_persistent(
        session.scene(), session.assets(), !live.has_value(), 0u, {}, {}, nullptr, false);
    EXPECT_FALSE(native_creation.selected_object->visible);
    EXPECT_TRUE(native_creation.selected_object->meshes.empty());
    EXPECT_FALSE(native_cache.update_persistent(
        session.scene(), session.assets(), false, 0u, {}, {}, nullptr, false).changed());

    request.construction_object_id = live->object_id;
    request.raster_scale = 2.0f;
    const auto subdued = coordinator.submit(request, settings, target);
    EXPECT_TRUE(subdued.persistent_submitted);
    EXPECT_TRUE(subdued.dynamic_submitted);
    EXPECT_EQ(selection.build_count, 1);
    ASSERT_EQ(target.persistent_frame.lines.size(), ordinary.lines.size());
    EXPECT_TRUE(target.persistent_frame.triangles.empty());
    for (std::size_t i = 0; i < topology.lines.size(); ++i) {
      const auto &line = target.persistent_frame.lines[i];
      EXPECT_EQ(line.element.object_id, live->object_id);
      EXPECT_EQ(line.color >> 24u, (ordinary.lines[i].color >> 24u) / 10u);
      EXPECT_GT(line.color >> 24u, 0u);
      EXPECT_LE(line.width_pixels, 2.0f);
    }
    for (std::size_t i = topology.lines.size(); i < ordinary.lines.size(); ++i) {
      EXPECT_EQ(target.persistent_frame.lines[i].color, ordinary.lines[i].color);
    }
    ASSERT_EQ(target.dynamic_frame.lines.size(), 2u);
    EXPECT_EQ(target.dynamic_frame.lines[0].color >> 24u,
              (ordinary_hover.lines[0].color >> 24u) / 10u);
    EXPECT_EQ(target.dynamic_frame.lines[1].color, ordinary_hover.lines[1].color);
    ASSERT_EQ(target.dynamic_frame.triangles.size(), 2u);
    EXPECT_EQ(target.dynamic_frame.triangles[0].color, kOverlayPrimitiveCubeFaceColor);
    EXPECT_EQ(target.dynamic_frame.triangles[1].element.object_id, live->object_id + 1000);

    const auto composed = coordinator.work_counters().dynamic_source_compose_count;
    EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
    EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, composed);
    EXPECT_EQ(selection.build_count, 1);
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    EXPECT_FALSE(editor.tools().live_primitive_creation());
    request.construction_object_id = -1;
    request.raster_scale = 1.0f;
    EXPECT_TRUE(coordinator.submit(request, settings, target).persistent_submitted);
    EXPECT_EQ(target.persistent_frame.triangles.size(), ordinary.triangles.size());
    EXPECT_EQ(target.dynamic_frame.triangles.size(), ordinary_hover.triangles.size());
    EXPECT_TRUE(native_cache.update_persistent(
        session.scene(), session.assets(), true, 0u, {}).selected_object->visible);
    for (std::size_t i = 0; i < ordinary.lines.size(); ++i) {
      EXPECT_EQ(target.persistent_frame.lines[i].color, ordinary.lines[i].color);
    }
    EXPECT_EQ(target.dynamic_frame.lines[0].color, ordinary_hover.lines[0].color);
  }
}

} // namespace
} // namespace quader

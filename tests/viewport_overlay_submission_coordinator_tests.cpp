////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"

#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <utility>
#include <vector>

namespace quader {
namespace {

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
  static const StaticDynamicToolFrameProvider provider = [] {
    ViewportOverlayFrame tool;
    tool.points.push_back({
        .position = {1.0f, 2.0f, 3.0f},
        .source = ViewportOverlaySource::Gizmo,
    });
    return StaticDynamicToolFrameProvider{std::move(tool)};
  }();
  return provider;
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
  const ViewportOverlaySubmissionResult style_result = coordinator.submit(
      std::move(style_changed), settings, target);
  EXPECT_TRUE(style_result.selection_source_refreshed);
  EXPECT_TRUE(style_result.persistent_submitted);
  EXPECT_TRUE(style_result.dynamic_submitted);
  ASSERT_EQ(target.persistent_frame.lines.size(), 1u);
  EXPECT_NE(target.persistent_frame.lines.front().color,
            initial_styled_color);
  EXPECT_EQ(selection_provider.build_count, 3);
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

} // namespace
} // namespace quader

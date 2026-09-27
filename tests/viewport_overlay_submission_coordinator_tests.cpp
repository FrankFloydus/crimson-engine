////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/overlay_face_fill_composition.hpp"
#include "render/overlay/viewport_selection_dimension_geometry.hpp"
#include "render/overlay/viewport_overlay_dash.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "render/render_scene_snapshot.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/runtime/selection_types.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "editor/viewport/viewport_modeling_tool_commit_adapter.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include "viewport/viewport_settings.hpp"
#include "assets/asset_registry.hpp"
#include "scene/scene_document.hpp"

#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <set>
#include <cmath>
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
      .selection_dimensions_visible = false,
  };
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     WireframeRetainsRearVerticesWithoutChangingSolidOcclusionOrSharedSources) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  ViewportOverlayFrame selection;
  selection.triangles.push_back({
      .layer = ViewportOverlayLayer::SourceWireDepthStamp,
      .a = {-2.0f, -2.0f, -1.0f},
      .b = {2.0f, -2.0f, -1.0f},
      .c = {0.0f, 2.0f, -1.0f},
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = 1},
  });
  selection.points.push_back({
      .layer = ViewportOverlayLayer::SelectedVertex,
      .position = {0.0f, 0.0f, -3.0f},
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = 1, .vertex_id = 1},
      .outline_growth_pixels = 0.0f,
  });
  StaticSelectionFrameProvider selection_provider{selection};
  ViewportOverlayFrame volatile_modeling = selection;
  volatile_modeling.points.front().layer = ViewportOverlayLayer::HoverVertex;
  volatile_modeling.points.front().source = ViewportOverlaySource::ComponentHover;
  RenderOverlaySelectionSnapshot scene;
  CurveToolPreviewState curve;
  auto request = submission_request(selection_provider, scene, volatile_modeling, curve);
  request.camera = {
      .eye = {0.0f, 0.0f, 0.0f},
      .forward = {0.0f, 0.0f, -1.0f},
      .right = {1.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
      .viewport_size = {800.0f, 600.0f},
      .projection = ViewportOverlayProjection::Perspective,
  };
  request.style_selection_context = ViewportOverlaySelectionContext::Vertex;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Vertex;
  // Mode filtering must also work without a scene-overlay snapshot request.
  request.scene_overlay_requested = false;
  const auto stamps = [](const ViewportOverlayFrame &frame) {
    return std::ranges::count_if(frame.triangles, [](const auto &triangle) {
      return triangle.layer == ViewportOverlayLayer::SourceWireDepthStamp;
    });
  };
  const auto expect_visibility = [&](bool wireframe) {
    for (const bool dynamic : {false, true}) {
      const auto &frame = dynamic ? target.dynamic_frame : target.persistent_frame;
      EXPECT_EQ(stamps(frame), wireframe ? 0 : 1);
      const auto plan = OverlayRenderableBuilder::build_layer(
          frame, request.camera, dynamic ? ViewportOverlayLayer::HoverVertex
                                         : ViewportOverlayLayer::SelectedVertex);
      EXPECT_EQ(plan.point_batches.empty(), !wireframe);
      for (const auto &batch : plan.point_batches) {
        EXPECT_EQ(batch.mesh.vertices.size(), 4u);
        EXPECT_TRUE(batch.state.depth_culling);
        EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
        EXPECT_EQ(batch.state.culling_mode, OverlayCullingMode::None);
      }
    }
  };
  auto result = coordinator.submit(request, settings, target);
  ASSERT_TRUE(result.persistent_submitted);
  ASSERT_TRUE(result.dynamic_submitted);
  expect_visibility(false);
  const auto initial_work = coordinator.work_counters();
  const int initial_builds = selection_provider.build_count;
  request.wireframe_pane = true;
  result = coordinator.submit(request, settings, target);
  EXPECT_TRUE(result.persistent_submitted);
  EXPECT_TRUE(result.dynamic_submitted);
  EXPECT_FALSE(result.selection_source_refreshed);
  expect_visibility(true);
  ASSERT_NE(result.edge_hud_frame, nullptr);
  EXPECT_EQ(stamps(*result.edge_hud_frame), 1);
  EXPECT_EQ(stamps(volatile_modeling), 1);
  EXPECT_EQ(selection_provider.build_count, initial_builds);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            initial_work.dynamic_source_compose_count);
  for (int frame = 0; frame < 8; ++frame) {
    EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  }
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            initial_work.dynamic_source_compose_count);
  request.wireframe_pane = false;
  result = coordinator.submit(request, settings, target);
  EXPECT_TRUE(result.persistent_submitted);
  EXPECT_TRUE(result.dynamic_submitted);
  expect_visibility(false);
  EXPECT_EQ(selection_provider.build_count, initial_builds);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            initial_work.dynamic_source_compose_count);

  // Another pane must retain solid occlusion even while the first is wireframe.
  request.wireframe_pane = true;
  (void)coordinator.submit(request, settings, target);
  request.viewport_index = 0u;
  request.wireframe_pane = false;
  result = coordinator.submit(request, settings, target);
  EXPECT_FALSE(result.selection_source_refreshed);
  expect_visibility(false);
  EXPECT_EQ(selection_provider.build_count, initial_builds);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     InsertEdgeLoopVerticesReachRetainedSubmissionAndClearWithoutStaleGeometry) {
  SceneDocument document;
  AssetRegistry assets;
  ModelingSessionService modeling(document, assets);
  ASSERT_TRUE(modeling.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
      .name = "Loop preview submission"}).ok);
  ASSERT_TRUE(modeling.activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop).ok);
  ModelingToolRayEvent event{
      .ray = {.origin = {1, 0, 5}, .direction = {0, 0, -1}},
      .grid_size = 0.5, .edge_radius = 0.2, .snap_enabled = true};
  ASSERT_TRUE(modeling.begin_insert_edge_loop_drag(event).ok);
  ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);

  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  StaticSelectionFrameProvider selection({});
  StaticDynamicToolFrameProvider tools({});
  RenderOverlaySelectionSnapshot scene;
  CurveToolPreviewState curve;
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::VertexSize, 9.0f));
  ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::HoverVertexGrowth, 2.0f));
  ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::VertexOutlineSize, 1.5f));
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::HoverVertex,
                                {0.2f, 0.8f, 0.4f, 0.75f}));
  const auto submit = [&](float scale = 1.0f) {
    const auto snapshot = modeling.volatile_overlay_snapshot();
    auto request = submission_request(selection, scene, *snapshot.frame, curve);
    request.volatile_modeling_source = {
        .source_identity = snapshot.source.source_identity,
        .publication_revision = snapshot.source.publication_revision};
    request.dynamic_tool_frame_provider = &tools;
    request.dynamic_tool_source = {
        .source_identity = reinterpret_cast<std::uintptr_t>(&tools),
        .publication_revision = 1u};
    request.raster_scale = scale;
    return coordinator.submit(request, settings, target);
  };
  const auto expect_loop = [&](float y, float scale) {
    ASSERT_EQ(target.dynamic_frame.points.size(), 4u);
    for (const auto &point : target.dynamic_frame.points) {
      EXPECT_EQ(point.source, ViewportOverlaySource::Tool);
      EXPECT_EQ(point.layer, ViewportOverlayLayer::HoverVertex);
      EXPECT_EQ(point.style_role, ViewportOverlayStyleRole::HoverVertex);
      EXPECT_NEAR(point.position.y, y, 0.0001f);
      EXPECT_FLOAT_EQ(point.size_pixels, 11.0f * scale);
      ASSERT_TRUE(point.outline_growth_pixels.has_value());
      EXPECT_FLOAT_EQ(*point.outline_growth_pixels, 1.5f * scale);
      EXPECT_GT(point.color >> 24u, 180u);
      EXPECT_LT(point.color >> 24u, 200u);
    }
    EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.lines,
        [](const auto &line) { return line.source == ViewportOverlaySource::Tool; }), 4);
    const auto plan = OverlayRenderableBuilder::build_layer(
        target.dynamic_frame,
        {.eye = {0, 0, 5}, .forward = {0, 0, -1},
         .right = {1, 0, 0}, .up = {0, 1, 0}, .viewport_size = {800, 600}},
        ViewportOverlayLayer::HoverVertex);
    ASSERT_EQ(plan.point_batches.size(), 2u);
    for (const auto &batch : plan.point_batches) {
      // Source depth stamps may prune the two rear glyphs before GPU submission.
      // Both front glyphs must survive in each standard outline/fill pass.
      EXPECT_GE(batch.mesh.vertices.size(), 8u);
      EXPECT_LE(batch.mesh.vertices.size(), 16u);
      for (const float x : {-1.0f, 1.0f}) {
        EXPECT_EQ(std::ranges::count_if(batch.mesh.vertices, [&](const auto &vertex) {
          return std::abs(vertex.center.x - x) < 0.0001f &&
                 std::abs(vertex.center.y - y) < 0.0001f &&
                 std::abs(vertex.center.z - 1.0f) < 0.0001f;
        }), 4);
      }
      EXPECT_TRUE(batch.state.depth_culling);
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
      EXPECT_FLOAT_EQ(batch.depth_bias, kOverlayPointDepthBias);
    }
  };
  ASSERT_TRUE(submit().dynamic_submitted);
  expect_loop(0.0f, 1.0f);
  const auto cache = modeling.volatile_overlay_cache_stats();
  const auto work = coordinator.work_counters();
  const int submissions = target.dynamic_submit_count;
  for (int frame = 0; frame < 8; ++frame) EXPECT_TRUE(submit().camera_only);
  EXPECT_EQ(modeling.volatile_overlay_cache_stats().build_count, cache.build_count);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            work.dynamic_source_compose_count);
  EXPECT_EQ(target.dynamic_submit_count, submissions);
  ASSERT_TRUE(submit(2.0f).dynamic_submitted);
  expect_loop(0.0f, 2.0f);
  ASSERT_TRUE(submit().dynamic_submitted);
  expect_loop(0.0f, 1.0f);
  EXPECT_EQ(modeling.volatile_overlay_cache_stats().build_count, cache.build_count);

  event.ray.origin.y = 0.5f;
  ASSERT_TRUE(modeling.update_insert_edge_loop_drag(event).ok);
  ASSERT_TRUE(submit().dynamic_submitted);
  expect_loop(0.5f, 1.0f);
  event.ray.origin.y = 0.95f;
  ASSERT_TRUE(modeling.update_insert_edge_loop_drag(event).ok);
  ASSERT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.dynamic_frame.points.empty());
  EXPECT_FALSE(std::ranges::any_of(target.dynamic_frame.lines,
      [](const auto &line) { return line.source == ViewportOverlaySource::Tool; }));
  EXPECT_TRUE(submit().camera_only);

  event.ray.origin.y = 0.0f;
  ASSERT_TRUE(modeling.update_insert_edge_loop_drag(event).ok);
  ASSERT_TRUE(submit().dynamic_submitted);
  expect_loop(0.0f, 1.0f);
  ASSERT_TRUE(modeling.commit_insert_edge_loop_drag().ok);
  ASSERT_TRUE(submit().dynamic_submitted);
  EXPECT_TRUE(target.dynamic_frame.points.empty());
  EXPECT_FALSE(std::ranges::any_of(target.dynamic_frame.lines,
      [](const auto &line) { return line.source == ViewportOverlaySource::Tool; }));
  EXPECT_TRUE(submit().camera_only);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     KnifeBoundaryZigzagRetainsEverySegmentAndCommitsOneUndoableSplit) {
  // Opposite box faces exercise both projected polygon windings. Every point
  // touches the original face perimeter, including the two intermediate bends.
  for (const float side : {1.0f, -1.0f}) {
    SCOPED_TRACE(side);
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    auto &modeling = session.modeling();
    const auto box = modeling.create_box_from_bounds({
        .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
        .name = "Knife zigzag submission"});
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Knife).ok());
    session.history().clear();
    session.mark_clean();
    const auto authored = [&] {
      return session.assets().mesh(session.scene().mesh(box.object.entity)->mesh)
          ->authored_polygon_document;
    };
    const auto before = authored();
    ASSERT_NE(before, nullptr);
    ASSERT_EQ(before->faces.size(), 6u);
    const auto content_revision = modeling.content_revision();
    const auto history_revision = session.history().revision();
    const std::array<QVec3, 4> points{{
        {-0.75f, 1.0f, side}, {-0.25f, -1.0f, side},
        {0.25f, 1.0f, side}, {0.75f, -1.0f, side}}};
    const auto same_position = [](const auto &left, const auto &right) {
      return std::abs(left.x - right.x) < 0.0001f &&
             std::abs(left.y - right.y) < 0.0001f &&
             std::abs(left.z - right.z) < 0.0001f;
    };

    ViewportOverlaySubmissionCoordinator coordinator;
    RecordingOverlayTarget target;
    StaticSelectionFrameProvider selection({});
    StaticDynamicToolFrameProvider tools({});
    RenderOverlaySelectionSnapshot scene;
    CurveToolPreviewState curve;
    const auto settings = ViewportSettings::defaults();
    const auto submit = [&] {
      const auto snapshot = modeling.volatile_overlay_snapshot();
      auto request = submission_request(selection, scene, *snapshot.frame, curve);
      request.volatile_modeling_source = {
          .source_identity = snapshot.source.source_identity,
          .publication_revision = snapshot.source.publication_revision};
      request.dynamic_tool_frame_provider = &tools;
      request.dynamic_tool_source = {
          .source_identity = reinterpret_cast<std::uintptr_t>(&tools),
          .publication_revision = 1u};
      return coordinator.submit(request, settings, target);
    };
    const auto is_knife_line = [](const auto &line) {
      return line.source == ViewportOverlaySource::Tool &&
             line.layer == ViewportOverlayLayer::ToolPreviewLine;
    };
    const auto is_knife_point = [](const auto &point) {
      return point.source == ViewportOverlaySource::Tool &&
             point.layer == ViewportOverlayLayer::ToolHandle;
    };
    for (std::size_t index = 0; index < points.size(); ++index) {
      SCOPED_TRACE(index);
      const auto accepted = modeling.accept_knife_point({
          .screen_position = {400.0f + side * points[index].x * 150.0f,
                              300.0f - points[index].y * 150.0f},
          .ray = {.origin = {points[index].x, points[index].y, side * 6.0f},
                  .direction = {0, 0, -side}},
          .camera = {.orthographic = true,
                     .origin = {0, 0, side * 6.0f},
                     .x_axis = {side, 0, 0}, .y_axis = {0, 1, 0},
                     .z_axis = {0, 0, side}, .viewport_size = {800, 600},
                     .orthographic_size = 4.0},
          .grid_size = 0.5, .vertex_radius = 0.001, .edge_radius = 0.01,
          .snap_enabled = false});
      ASSERT_TRUE(accepted.ok) << accepted.message;
      ASSERT_TRUE(accepted.changed);
      ASSERT_TRUE(submit().dynamic_submitted);
      EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.lines, is_knife_line), index);
      EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.points, is_knife_point), index + 1u);
      for (std::size_t segment = 0; segment < index; ++segment) {
        EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.lines, [&](const auto &line) {
          return is_knife_line(line) && same_position(line.start, points[segment]) &&
                 same_position(line.end, points[segment + 1u]);
        }), 1);
      }
      for (std::size_t point = 0; point <= index; ++point) {
        EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.points, [&](const auto &value) {
          return is_knife_point(value) && same_position(value.position, points[point]);
        }), 1);
      }
      EXPECT_TRUE(quader_poly::has_same_geometry(*authored(), *before));
      EXPECT_EQ(modeling.content_revision(), content_revision);
      EXPECT_EQ(session.history().revision(), history_revision);
      EXPECT_EQ(session.history().undo_count(), 0u);
      EXPECT_FALSE(session.dirty());
    }
    ASSERT_TRUE(modeling.tool_status().knife_can_commit);
    const auto cache = modeling.volatile_overlay_cache_stats();
    const auto work = coordinator.work_counters();
    const int submissions = target.dynamic_submit_count;
    for (int frame = 0; frame < 8; ++frame) EXPECT_TRUE(submit().camera_only);
    EXPECT_EQ(modeling.volatile_overlay_cache_stats().build_count, cache.build_count);
    EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
              work.dynamic_source_compose_count);
    EXPECT_EQ(target.dynamic_submit_count, submissions);

    // This production port invokes commit_knife_tool inside the native history
    // command, exactly as finalizing the viewport point sequence does.
    ViewportModelingToolCommitAdapter commit(editor.tools());
    const auto committed = commit.commit_knife_tool();
    ASSERT_TRUE(committed.ok) << committed.message;
    ASSERT_TRUE(committed.changed);
    const auto after = authored();
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->faces.size(), 9u);
    EXPECT_EQ(after->vertices.size(), before->vertices.size() + points.size());
    for (std::size_t segment = 0; segment + 1u < points.size(); ++segment) {
      int incident_faces = 0;
      for (const auto &face : after->faces) {
        for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
          const auto *a = quader_poly::find_vertex(*after, face.vertices[corner]);
          const auto *b = quader_poly::find_vertex(
              *after, face.vertices[(corner + 1u) % face.vertices.size()]);
          ASSERT_NE(a, nullptr);
          ASSERT_NE(b, nullptr);
          if ((same_position(a->position, points[segment]) &&
               same_position(b->position, points[segment + 1u])) ||
              (same_position(b->position, points[segment]) &&
               same_position(a->position, points[segment + 1u]))) ++incident_faces;
        }
      }
      EXPECT_EQ(incident_faces, 2);
    }
    EXPECT_EQ(session.history().undo_count(), 1u);
    EXPECT_EQ(modeling.tool_status().active_tool, ModelingViewportTool::Select);
    ASSERT_TRUE(submit().dynamic_submitted);
    EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.lines, is_knife_line), 0);
    EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.points, is_knife_point), 0);
    EXPECT_TRUE(submit().camera_only);

    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(quader_poly::has_same_geometry(*authored(), *before));
    EXPECT_EQ(session.history().undo_count(), 0u);
    EXPECT_EQ(session.history().redo_count(), 1u);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(quader_poly::has_same_geometry(*authored(), *after));
    EXPECT_EQ(session.history().undo_count(), 1u);
    EXPECT_EQ(session.history().redo_count(), 0u);
  }
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
    request.selection_dimensions_visible = false;

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
     PassivePaneGizmoPublishesAndReusesWithoutOtherPanesGestureVisuals) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.0f, 0.0f, -1.0f});
  const auto overlay_camera =
      ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  quader_editor_core::TransformGizmoFrameInput gizmo_input;
  gizmo_input.tool = quader_editor_core::ViewportInteractionTool::Move;
  gizmo_input.has_selection = true;
  gizmo_input.has_pivot = true;
  gizmo_input.camera =
      ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size());
  auto gizmo = quader_editor_core::build_transform_gizmo_frame(gizmo_input);
  ASSERT_TRUE(gizmo.info.ok);
  ViewportSelectionMarquee marquee{
      .active = true,
      .start = {20.0f, 30.0f},
      .end = {100.0f, 130.0f},
  };
  const std::vector<quader_editor_core::SelectionOverlayPoint> snap_points{{
      .layer = quader_editor_core::SelectionOverlayLayer::SnapTargetVertex,
      .position = {1.0f, 1.0f, 0.0f},
  }};
  ViewportToolOverlaySourceInputs inputs{
      .source_identity = reinterpret_cast<std::uintptr_t>(&gizmo),
      .overlay_camera = &overlay_camera,
      .viewport_camera = &camera,
      .transform_gizmo = &gizmo,
      .transform_snap_points = &snap_points,
      .selection_marquee = &marquee,
      .interaction_pane = false,
      .gizmos_visible = true,
      .overlays_visible = true,
  };
  ViewportToolOverlaySource passive{inputs};
  const auto passive_frame = passive.build_dynamic_tool_overlay_frame();
  EXPECT_FALSE(passive_frame.lines.empty() && passive_frame.triangles.empty());
  const auto marquee_line = [](const auto &line) {
    return line.layer == ViewportOverlayLayer::ToolPreviewLine &&
           line.color == kOverlaySelectionMarqueeLineColor;
  };
  const auto marquee_fill = [](const auto &triangle) {
    return triangle.layer == ViewportOverlayLayer::ToolPreviewFill &&
           triangle.color == kOverlaySelectionMarqueeFillColor;
  };
  const auto snap_point = [](const auto &point) {
    return point.layer == ViewportOverlayLayer::SnapTargetVertex;
  };
  EXPECT_EQ(std::count_if(passive_frame.lines.begin(), passive_frame.lines.end(),
                          marquee_line), 0);
  EXPECT_EQ(std::count_if(passive_frame.triangles.begin(), passive_frame.triangles.end(),
                          marquee_fill), 0);
  EXPECT_EQ(std::count_if(passive_frame.points.begin(), passive_frame.points.end(),
                          snap_point), 0);

  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  StaticSelectionFrameProvider selection{{}};
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_modeling;
  CurveToolPreviewState curve;
  auto request = submission_request(selection, scene, volatile_modeling, curve);
  request.camera = overlay_camera;
  request.dynamic_tool_source = passive.source_stamp();
  request.dynamic_tool_frame_provider = &passive;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);
  const auto first_compose_count =
      coordinator.work_counters().dynamic_source_compose_count;
  EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            first_compose_count);

  // Gesture changes in another pane do not invalidate this pane's idle gizmo.
  marquee.end = {200.0f, 180.0f};
  const ViewportToolOverlaySource other_gesture_changed{inputs};
  EXPECT_EQ(other_gesture_changed.source_stamp(), passive.source_stamp());
  gizmo_input.tool = quader_editor_core::ViewportInteractionTool::Rotate;
  gizmo = quader_editor_core::build_transform_gizmo_frame(gizmo_input);
  const ViewportToolOverlaySource rotated{inputs};
  EXPECT_NE(rotated.source_stamp(), passive.source_stamp());
  request.dynamic_tool_source = rotated.source_stamp();
  request.dynamic_tool_frame_provider = &rotated;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);

  inputs.interaction_pane = true;
  const auto gesture_frame =
      ViewportToolOverlaySource{inputs}.build_dynamic_tool_overlay_frame();
  EXPECT_EQ(std::count_if(gesture_frame.lines.begin(), gesture_frame.lines.end(),
                          marquee_line), 4);
  EXPECT_EQ(std::count_if(gesture_frame.triangles.begin(), gesture_frame.triangles.end(),
                          marquee_fill), 2);
  EXPECT_EQ(std::count_if(gesture_frame.points.begin(), gesture_frame.points.end(),
                          snap_point), 1);
  inputs.interaction_pane = false;
  inputs.gizmos_visible = false;
  const ViewportToolOverlaySource hidden{inputs};
  EXPECT_NE(hidden.source_stamp(), rotated.source_stamp());
  const auto hidden_frame = hidden.build_dynamic_tool_overlay_frame();
  EXPECT_TRUE(hidden_frame.lines.empty());
  EXPECT_TRUE(hidden_frame.triangles.empty());
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     LinearArrayConstraintGuideSpansThePaneThroughItsRetainedEndpoint) {
  for (const auto projection : {ViewportOverlayProjection::Perspective,
                                ViewportOverlayProjection::Orthographic}) {
    for (const auto axis : {LinearArrayAxisConstraint::X, LinearArrayAxisConstraint::Y,
                            LinearArrayAxisConstraint::Z}) {
      SCOPED_TRACE(static_cast<int>(projection));
      SCOPED_TRACE(static_cast<int>(axis));
      LinearArrayHandleState handle{.visible = true, .source_world = {},
          .endpoint_world = {2.0f, 5.0f, 1.0f}, .constraint = axis, .revision = 1u};
      ViewportOverlayCamera camera{.eye = {2.0f, 5.0f, 21.0f},
          .viewport_size = {800.0f, 400.0f}, .projection = projection,
          .orthographic_size = 10.0f, .fov_degrees = 90.0f,
          .near_plane = 0.1f, .far_plane = 100.0f};
      if (axis == LinearArrayAxisConstraint::Z) {
        camera.eye = {22.0f, 5.0f, 1.0f};
        camera.forward = {-1.0f, 0.0f, 0.0f};
        camera.right = {0.0f, 0.0f, -1.0f};
      }
      ViewportToolOverlaySourceInputs inputs{
          .source_identity = reinterpret_cast<std::uintptr_t>(&handle),
          .overlay_camera = &camera, .linear_array_handle = &handle};
      ViewportOverlaySubmissionCoordinator coordinator;
      RecordingOverlayTarget target;
      StaticSelectionFrameProvider selection{{}};
      RenderOverlaySelectionSnapshot scene;
      ViewportOverlayFrame volatile_modeling;
      CurveToolPreviewState curve;
      auto request = submission_request(selection, scene, volatile_modeling, curve);
      const auto settings = ViewportSettings::defaults();
      const auto submit = [&]() {
        const ViewportToolOverlaySource source{inputs};
        request.camera = camera;
        request.raster_scale = inputs.raster_scale;
        request.dynamic_tool_source = source.source_stamp();
        request.dynamic_tool_frame_provider = &source;
        return coordinator.submit(request, settings, target);
      };
      EXPECT_TRUE(submit().dynamic_submitted);
      ASSERT_EQ(target.dynamic_frame.lines.size(), 2u);
      ASSERT_EQ(target.dynamic_frame.points.size(), 2u);
      const auto guide = target.dynamic_frame.lines.front();
      EXPECT_EQ(guide.color, axis == LinearArrayAxisConstraint::X ? kOverlayToolAxisXColor
          : axis == LinearArrayAxisConstraint::Y ? kOverlayToolAxisYColor : kOverlayToolAxisZColor);
      const QVec3 direction = axis == LinearArrayAxisConstraint::X ? QVec3{1, 0, 0}
          : axis == LinearArrayAxisConstraint::Y ? QVec3{0, 1, 0} : QVec3{0, 0, 1};
      EXPECT_LT(length(cross(guide.start - handle.endpoint_world, direction)), 0.0001f);
      EXPECT_LT(length(cross(guide.end - handle.endpoint_world, direction)), 0.0001f);
      EXPECT_LT(dot(guide.start - handle.endpoint_world, direction), 0.0f);
      EXPECT_GT(dot(guide.end - handle.endpoint_world, direction), 0.0f);
      const OverlayCameraProjector projector(camera);
      const auto a = projector.project(projector.view_point_for(guide.start));
      const auto b = projector.project(projector.view_point_for(guide.end));
      ASSERT_TRUE(a.valid && b.valid);
      if (axis == LinearArrayAxisConstraint::Y) {
        EXPECT_NEAR(std::min(a.pixel.y, b.pixel.y), 0.0f, 0.001f);
        EXPECT_NEAR(std::max(a.pixel.y, b.pixel.y), 400.0f, 0.001f);
      } else {
        EXPECT_NEAR(std::min(a.pixel.x, b.pixel.x), 0.0f, 0.001f);
        EXPECT_NEAR(std::max(a.pixel.x, b.pixel.x), 800.0f, 0.001f);
      }
      const auto unchanged_count = coordinator.work_counters().dynamic_source_compose_count;
      for (int frame = 0; frame < 16; ++frame) EXPECT_TRUE(submit().camera_only);
      EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, unchanged_count);

      // Only the camera changes: clipping must follow it without needing a
      // tool mutation or an unrelated box/gizmo to invalidate the source.
      camera.eye = camera.eye + direction * 2.0f;
      EXPECT_TRUE(submit().dynamic_submitted);
      ASSERT_EQ(target.dynamic_frame.lines.size(), 2u);
      EXPECT_GT(length(target.dynamic_frame.lines.front().start - guide.start), 1.0f);
      inputs.raster_scale = 2.0f;
      EXPECT_TRUE(submit().dynamic_submitted);
      EXPECT_FLOAT_EQ(target.dynamic_frame.lines.front().width_pixels, 4.0f);
      inputs.raster_scale = 1.0f;
      EXPECT_TRUE(submit().dynamic_submitted);
      EXPECT_FLOAT_EQ(target.dynamic_frame.lines.front().width_pixels, 2.0f);
      handle.constraint = LinearArrayAxisConstraint::Free;
      ++handle.revision;
      EXPECT_TRUE(submit().dynamic_submitted);
      ASSERT_EQ(target.dynamic_frame.lines.size(), 1u);
      const auto free_count = coordinator.work_counters().dynamic_source_compose_count;
      camera.eye = camera.eye + direction;
      EXPECT_TRUE(submit().camera_only);
      EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, free_count);
      handle.visible = false;
      ++handle.revision;
      EXPECT_TRUE(submit().dynamic_submitted);
      EXPECT_TRUE(target.dynamic_frame.lines.empty());
      EXPECT_TRUE(target.dynamic_frame.points.empty());
    }
  }
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     LinearArrayConstraintGuideClipsNearPlaneAndRejectsInvisibleOrEndOnAxes) {
  LinearArrayHandleState handle{.visible = true, .endpoint_world = {1.0f, 1.0f, -3.0f},
      .constraint = LinearArrayAxisConstraint::Z, .revision = 1u};
  ViewportOverlayCamera camera{.viewport_size = {800.0f, 600.0f},
      .fov_degrees = 90.0f, .near_plane = 2.0f, .far_plane = 20.0f};
  ViewportToolOverlaySourceInputs inputs{.overlay_camera = &camera,
                                         .linear_array_handle = &handle};
  auto frame = ViewportToolOverlaySource{inputs}.build_dynamic_tool_overlay_frame();
  ASSERT_EQ(frame.lines.size(), 2u);
  const auto &guide = frame.lines.front();
  EXPECT_NEAR(std::min(guide.start.z, guide.end.z), -20.0f, 0.0001f);
  EXPECT_NEAR(std::max(guide.start.z, guide.end.z), -2.0f, 0.0001f);
  handle.endpoint_world.z = 3.0f;
  frame = ViewportToolOverlaySource{inputs}.build_dynamic_tool_overlay_frame();
  EXPECT_EQ(frame.lines.size(), 2u) << "An axis can cross the frustum even with its anchor behind the eye.";
  handle.endpoint_world = {0.0f, 0.0f, -3.0f};
  frame = ViewportToolOverlaySource{inputs}.build_dynamic_tool_overlay_frame();
  EXPECT_EQ(frame.lines.size(), 1u) << "An end-on guide must not become a spurious screen line.";
  handle.constraint = LinearArrayAxisConstraint::X;
  handle.endpoint_world = {0.0f, 0.0f, 3.0f};
  frame = ViewportToolOverlaySource{inputs}.build_dynamic_tool_overlay_frame();
  EXPECT_EQ(frame.lines.size(), 1u) << "A line entirely behind the camera has no visible guide.";
  camera.projection = ViewportOverlayProjection::Orthographic;
  handle.endpoint_world = {0.0f, 1000.0f, -3.0f};
  frame = ViewportToolOverlaySource{inputs}.build_dynamic_tool_overlay_frame();
  EXPECT_EQ(frame.lines.size(), 1u) << "An offscreen parallel guide must remain clipped.";
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
    request.selection_dimensions_visible = false;
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

void expect_dimension_bounds(const ViewportOverlaySubmissionResult &result,
                              QVec3 minimum, QVec3 maximum) {
  ASSERT_TRUE(result.selection_dimension_bounds.has_value());
  const auto &bounds = *result.selection_dimension_bounds;
  EXPECT_FLOAT_EQ(bounds.min.x, minimum.x);
  EXPECT_FLOAT_EQ(bounds.min.y, minimum.y);
  EXPECT_FLOAT_EQ(bounds.min.z, minimum.z);
  EXPECT_FLOAT_EQ(bounds.max.x, maximum.x);
  EXPECT_FLOAT_EQ(bounds.max.y, maximum.y);
  EXPECT_FLOAT_EQ(bounds.max.z, maximum.z);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     SelectionDimensionsAggregateMeshesGroupsAndInstancesIntoOneBox) {
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.selected_mesh_bounds = {
      {.entity = 1, .min = {-2, -1, 0}, .max = {2, 1, 3}},
      {.entity = 2, .min = {4, 2, -3}, .max = {7, 6, 2}}};
  scene.group_bounds = {
      {.entity = 3, .min = {-5, -4, -2}, .max = {-3, 0, 1}},
      {.entity = 4, .min = {0, 0, 0}, .max = {4, 8, 9}}};
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  auto result = coordinator.submit(request, settings, target);
  expect_dimension_bounds(result, {-5, -4, -3}, {7, 8, 9});
  EXPECT_EQ(std::ranges::count_if(target.persistent_frame.lines, [](const auto &line) {
    return line.layer == ViewportOverlayLayer::GroupBounds;
  }), 0);
  EXPECT_EQ(std::ranges::count_if(target.dynamic_frame.lines, [](const auto &line) {
    return line.source == ViewportOverlaySource::SelectionDimensions &&
           line.style_role == ViewportOverlayStyleRole::ExplicitColor;
  }), 3);
  request.selection_dimensions_visible = false;
  EXPECT_FALSE(coordinator.submit(request, settings, target).selection_dimension_bounds);
  EXPECT_EQ(std::ranges::count_if(target.persistent_frame.lines, [](const auto &line) {
    return line.layer == ViewportOverlayLayer::GroupBounds;
  }), 36);
  EXPECT_FALSE(std::ranges::any_of(target.dynamic_frame.lines, [](const auto &line) {
    return line.source == ViewportOverlaySource::SelectionDimensions;
  }));
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Group;
  result = coordinator.submit(request, settings, target);
  expect_dimension_bounds(result, {-5, -4, -3}, {7, 8, 9});
}

ViewportOverlayCamera dimension_test_camera() {
  ViewportOverlayCamera camera;
  camera.eye = {20.0f, 18.0f, 25.0f};
  camera.forward = normalize(QVec3{} - camera.eye);
  camera.right = normalize(cross(camera.forward, QVec3{0, 1, 0}));
  camera.up = normalize(cross(camera.right, camera.forward));
  camera.viewport_size = {1000, 800};
  return camera;
}

std::size_t expect_dimension_box_edges(const ViewportOverlayFrame &frame,
                                      const ViewportSelectionDimensionBounds &bounds,
                                      const ViewportOverlayCamera &camera,
                                      float raster_scale = 1.0f) {
  const std::array<float, 3> minimum{bounds.min.x, bounds.min.y, bounds.min.z};
  const std::array<float, 3> maximum{bounds.max.x, bounds.max.y, bounds.max.z};
  std::set<std::pair<std::size_t, unsigned int>> edges;
  const OverlayCameraProjector projector(camera);
  const auto spans = viewport_selection_dimension_spans(
      bounds, camera, viewport_host_defaults().map_units_per_world_unit);
  constexpr std::array kColors{kOverlayToolAxisXColor, kOverlayToolAxisYColor,
                               kOverlayToolAxisZColor};
  std::array<int, 3> solid_counts{};
  for (const auto &line : frame.lines) {
    if (line.source != ViewportOverlaySource::SelectionDimensions) continue;
    EXPECT_EQ(line.layer, ViewportOverlayLayer::GroupBounds);
    EXPECT_EQ(viewport_overlay_depth_mode(line), ViewportOverlayDepthMode::DrawOnTop);
    const std::array<float, 3> start{line.start.x, line.start.y, line.start.z};
    const std::array<float, 3> end{line.end.x, line.end.y, line.end.z};
    std::size_t axis = 3u;
    unsigned int fixed_maximum = 0u;
    for (std::size_t component = 0u; component < 3u; ++component) {
      EXPECT_GE(start[component], minimum[component] - 0.00001f);
      EXPECT_LE(start[component], maximum[component] + 0.00001f);
      EXPECT_GE(end[component], minimum[component] - 0.00001f);
      EXPECT_LE(end[component], maximum[component] + 0.00001f);
      if (start[component] != end[component]) {
        EXPECT_EQ(axis, 3u) << "Dimension segments must follow a single world axis";
        axis = component;
      } else {
        EXPECT_TRUE(std::abs(start[component] - minimum[component]) < 0.00001f ||
                    std::abs(start[component] - maximum[component]) < 0.00001f);
        if (start[component] == maximum[component] && minimum[component] != maximum[component]) {
          fixed_maximum |= 1u << component;
        }
      }
    }
    EXPECT_LT(axis, 3u) << "Dimension box cannot contain zero-length segments";
    if (axis >= 3u) continue;
    edges.emplace(axis, fixed_maximum);
    if (line.style_role == ViewportOverlayStyleRole::ExplicitColor) {
      ++solid_counts[axis];
      EXPECT_EQ(line.color, kColors[axis]);
      EXPECT_FLOAT_EQ(line.width_pixels, kOverlaySelectionDimensionLineWidthPixels * raster_scale);
      EXPECT_TRUE(spans[axis].has_value());
      if (spans[axis]) {
        EXPECT_LT(length(line.start - spans[axis]->start), 0.00001f);
        EXPECT_LT(length(line.end - spans[axis]->end), 0.00001f);
      }
    } else {
      EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::GroupBounds);
      const QVec2 screen_start = projector.project(projector.view_point_for(line.start)).pixel;
      const QVec2 screen_end = projector.project(projector.view_point_for(line.end)).pixel;
      const QVec2 delta = screen_end - screen_start;
      EXPECT_LE(std::hypot(delta.x, delta.y), kViewportOverlayDashPixels * raster_scale + 0.01f);
    }
  }
  for (std::size_t axis = 0u; axis < 3u; ++axis) {
    EXPECT_EQ(solid_counts[axis], spans[axis] ? 1 : 0);
  }
  return edges.size();
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     TiltedFaceDimensionsDrawAllBoxEdgesWithMatchingColoredMeasurementSpans) {
  ViewportOverlayFrame face;
  face.triangles.push_back({.layer = ViewportOverlayLayer::SelectedVisibleFace,
      .a = {-4, -3, -2}, .b = {4, -3, -2}, .c = {-4, 3, 2},
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = 1, .face_id = 1}});
  StaticSelectionFrameProvider provider(face);
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.style_selection_context = ViewportOverlaySelectionContext::Face;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Face;
  request.camera = dimension_test_camera();
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  auto settings = ViewportSettings::defaults();
  auto result = coordinator.submit(request, settings, target);
  ASSERT_TRUE(result.selection_dimension_bounds);
  EXPECT_EQ(expect_dimension_box_edges(target.dynamic_frame, *result.selection_dimension_bounds,
                                      request.camera), 12u);
  EXPECT_GT(target.dynamic_frame.lines.size(), 12u);
  EXPECT_TRUE(std::ranges::all_of(target.persistent_frame.lines, [](const auto &line) {
    return line.source != ViewportOverlaySource::SelectionDimensions;
  }));
  const auto before = coordinator.work_counters();
  for (int frame = 0; frame < 10; ++frame) {
    EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  }
  EXPECT_EQ(coordinator.work_counters().selection_dimension_frame_build_count,
            before.selection_dimension_frame_build_count);
  request.camera.eye.x += 1.0f;
  const auto moved = coordinator.submit(request, settings, target);
  EXPECT_TRUE(moved.dynamic_submitted);
  EXPECT_FALSE(moved.persistent_submitted);
  EXPECT_FALSE(moved.selection_source_refreshed);
  EXPECT_EQ(coordinator.work_counters().selection_dimension_frame_build_count,
            before.selection_dimension_frame_build_count + 1u);
  EXPECT_EQ(coordinator.work_counters().selection_dimension_bounds_build_count,
            before.selection_dimension_bounds_build_count);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            before.dynamic_source_compose_count);
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::BoundingBoxWire, {0.2f, 0.4f, 0.6f, 1}));
  (void)coordinator.submit(request, settings, target);
  EXPECT_EQ(coordinator.work_counters().selection_dimension_frame_build_count,
            before.selection_dimension_frame_build_count + 1u);
  for (const auto &line : target.dynamic_frame.lines) {
    if (line.source == ViewportOverlaySource::SelectionDimensions &&
        line.style_role == ViewportOverlayStyleRole::GroupBounds) {
      EXPECT_EQ(line.color, rgba8(51, 102, 153));
    }
  }
  request.raster_scale = 2.0f;
  result = coordinator.submit(request, settings, target);
  EXPECT_EQ(expect_dimension_box_edges(target.dynamic_frame, *result.selection_dimension_bounds,
                                      request.camera, 2.0f), 12u);
  request.selection_dimensions_visible = false;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);
  EXPECT_FALSE(std::ranges::any_of(target.dynamic_frame.lines, [](const auto &line) {
    return line.source == ViewportOverlaySource::SelectionDimensions;
  }));
  request.selection_dimensions_visible = true;
  (void)coordinator.submit(request, settings, target);
  request.overlays_visible = false;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);
  EXPECT_FALSE(std::ranges::any_of(target.dynamic_frame.lines, [](const auto &line) {
    return line.source == ViewportOverlaySource::SelectionDimensions;
  }));
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     FlatSelectionDimensionsHaveFourUniqueEdgesAndTwoColoredSpans) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.selected_mesh_bounds.push_back({.entity = 1, .min = {-4, -3, 0}, .max = {4, 3, 0}});
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  request.camera = dimension_test_camera();
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  auto result = coordinator.submit(request, settings, target);
  ASSERT_TRUE(result.selection_dimension_bounds);
  EXPECT_EQ(expect_dimension_box_edges(target.dynamic_frame, *result.selection_dimension_bounds,
                                      request.camera), 4u);
  request.camera = {.eye = {0, 0, 20}, .viewport_size = {800, 600},
                    .projection = ViewportOverlayProjection::Orthographic};
  result = coordinator.submit(request, settings, target);
  EXPECT_EQ(expect_dimension_box_edges(target.dynamic_frame, *result.selection_dimension_bounds,
                                      request.camera), 4u);
  scene.selected_mesh_bounds.front().max.y = -3;
  ++scene.selection_revision;
  result = coordinator.submit(request, settings, target);
  EXPECT_EQ(expect_dimension_box_edges(target.dynamic_frame, *result.selection_dimension_bounds,
                                      request.camera), 1u);
  scene.selected_mesh_bounds.front().max.x = -4;
  ++scene.selection_revision;
  result = coordinator.submit(request, settings, target);
  EXPECT_EQ(expect_dimension_box_edges(target.dynamic_frame, *result.selection_dimension_bounds,
                                      request.camera), 0u);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     DimensionDashesClipNearPlaneAndViewportBeforeSubdivision) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.selected_mesh_bounds.push_back({.entity = 1, .min = {-10000, -0.02f, -100},
                                        .max = {10000, 0.02f, 1}});
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  request.camera.viewport_size = {800, 600};
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto result = coordinator.submit(request, ViewportSettings::defaults(), target);
  ASSERT_TRUE(result.selection_dimension_bounds);
  const OverlayCameraProjector projector(request.camera);
  std::size_t dashed_count = 0u;
  for (const auto &line : target.dynamic_frame.lines) {
    if (line.source != ViewportOverlaySource::SelectionDimensions ||
        line.style_role != ViewportOverlayStyleRole::GroupBounds) continue;
    ++dashed_count;
    for (QVec3 point : {line.start, line.end}) {
      const auto view = projector.view_point_for(point);
      EXPECT_GE(-view.z, request.camera.near_plane - 0.0001f);
      EXPECT_LE(-view.z, request.camera.far_plane + 0.0001f);
      const auto projected = projector.project(view);
      EXPECT_TRUE(std::isfinite(projected.pixel.x));
      EXPECT_TRUE(std::isfinite(projected.pixel.y));
      EXPECT_GE(projected.pixel.x, -1.0f);
      EXPECT_LE(projected.pixel.x, 801.0f);
      EXPECT_GE(projected.pixel.y, -1.0f);
      EXPECT_LE(projected.pixel.y, 601.0f);
    }
  }
  EXPECT_GT(dashed_count, 0u);
  // Each of twelve clipped segments is no longer than the pane diagonal.
  EXPECT_LE(dashed_count, 12u * 78u);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     FaceDimensionsUseOnlySelectedFacesAcrossObjects) {
  ViewportOverlayFrame frame;
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::SelectedVisibleFace,
      .a = {0, 0, 2}, .b = {3, 0, 2}, .c = {0, 4, 2},
      .source = ViewportOverlaySource::Selection,
      .element = {.object_id = 1, .face_id = 1}});
  frame.lines.push_back({
      .layer = ViewportOverlayLayer::SelectedFaceEdge,
      .start = {-2, 3, -1}, .end = {-1, 8, 2},
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = 2, .face_id = 7}});
  for (auto layer : {ViewportOverlayLayer::SourceWire, ViewportOverlayLayer::HoverFaceEdge,
                     ViewportOverlayLayer::SelectedEdge}) {
    frame.lines.push_back({.layer = layer, .start = {-100, -100, -100},
                           .end = {100, 100, 100},
                           .source = ViewportOverlaySource::ComponentSelection});
  }
  frame.triangles.push_back({.layer = ViewportOverlayLayer::SourceWireDepthStamp,
      .a = {-50, 0, 0}, .b = {50, 0, 0}, .c = {0, 50, 0},
      .source = ViewportOverlaySource::Selection});
  StaticSelectionFrameProvider provider(frame);
  RenderOverlaySelectionSnapshot scene;
  scene.selected_mesh_bounds.push_back({.min = {-200, -200, -200}, .max = {200, 200, 200}});
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.style_selection_context = ViewportOverlaySelectionContext::Face;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Face;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  expect_dimension_bounds(coordinator.submit(request, settings, target), {-2, 0, -1}, {3, 8, 2});
  for (auto context : {ViewportOverlaySelectionContext::Edge, ViewportOverlaySelectionContext::Vertex}) {
    request.style_selection_context = context;
    EXPECT_FALSE(coordinator.submit(request, settings, target).selection_dimension_bounds);
  }
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     SelectionDimensionBoundsRemainRetainedAcrossCameraStylePanesAndVisibility) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.selected_mesh_bounds.push_back({.entity = 1, .min = {1, 2, 3}, .max = {4, 6, 8}});
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  auto settings = ViewportSettings::defaults();
  const auto initial = coordinator.submit(request, settings, target);
  const auto work = coordinator.work_counters();
  for (int frame = 0; frame < 12; ++frame) {
    request.camera.eye.x = static_cast<float>(frame);
    request.viewport_index = static_cast<std::size_t>(frame % 4);
    const auto unchanged = coordinator.submit(request, settings, target);
    expect_dimension_bounds(unchanged, {1, 2, 3}, {4, 6, 8});
    EXPECT_EQ(unchanged.selection_dimension_signature, initial.selection_dimension_signature);
  }
  (void)settings.set_color(ViewportColorSettingId::SelectionWire, {0.3f, 0.2f, 0.8f, 1});
  request.raster_scale = 2.0f;
  request.camera.projection = ViewportOverlayProjection::Orthographic;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {1, 2, 3}, {4, 6, 8});
  request.selection_dimensions_visible = false;
  EXPECT_FALSE(coordinator.submit(request, settings, target).selection_dimension_bounds);
  request.selection_dimensions_visible = true;
  request.overlays_visible = false;
  EXPECT_FALSE(coordinator.submit(request, settings, target).selection_dimension_bounds);
  request.overlays_visible = true;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {1, 2, 3}, {4, 6, 8});
  EXPECT_EQ(coordinator.work_counters().selection_dimension_bounds_build_count,
            work.selection_dimension_bounds_build_count);
  EXPECT_EQ(coordinator.work_counters().selection_dimension_source_record_count,
            work.selection_dimension_source_record_count);
  scene.selected_mesh_bounds.clear();
  ++scene.selection_revision;
  const auto cleared = coordinator.submit(request, settings, target);
  EXPECT_FALSE(cleared.selection_dimension_bounds);
  EXPECT_NE(cleared.selection_dimension_signature, initial.selection_dimension_signature);
}

RenderToolPreviewMeshRecord dimension_preview(int object, EntityId entity) {
  RenderToolPreviewMeshRecord preview;
  preview.source_object_id = object;
  preview.source_entity = entity;
  preview.dynamic_positions = true;
  preview.suppress_source = true;
  preview.layout_key = 1u;
  preview.mesh.edit_geometry().vertices = {
      {.position = {-8, 0, 0}, .source_vertex_id = 1},
      {.position = {8, 4, 0}, .source_vertex_id = 2},
      {.position = {8, 0, 20}, .source_vertex_id = 3}};
  preview.mesh.edit_geometry().indices = {0, 1, 2};
  preview.dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(
      std::vector<QVec3>{{2, 1, 0}, {4, 3, 0}, {3, 2, 1}});
  return preview;
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     ObjectDimensionsReplaceCommittedPreviewExtremaAndKeepUntouchedSelection) {
  ViewportOverlayFrame frame;
  frame.lines.push_back({.layer = ViewportOverlayLayer::SelectedEdge,
      .start = {-8, 0, 0}, .end = {8, 4, 20},
      .source = ViewportOverlaySource::Selection,
      .element = {.object_id = 10, .edge_a = 1, .edge_b = 2}});
  StaticSelectionFrameProvider provider(frame);
  RenderOverlaySelectionSnapshot scene;
  scene.selected_mesh_bounds = {
      {.entity = 100, .min = {-8, 0, 0}, .max = {8, 4, 20}},
      {.entity = 200, .min = {10, 0, 0}, .max = {12, 2, 2}}};
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  std::vector previews{dimension_preview(10, 100), dimension_preview(99, 999)};
  previews.back().suppress_source = false;
  previews.back().dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(
      std::vector<QVec3>{{-100, -100, -100}, {100, 100, 100}, {0, 0, 0}});
  request.selection_dimension_preview_meshes = previews;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  expect_dimension_bounds(coordinator.submit(request, settings, target), {2, 0, 0}, {12, 3, 2});
  const auto work = coordinator.work_counters();
  for (int frame_index = 0; frame_index < 8; ++frame_index) {
    EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  }
  EXPECT_EQ(coordinator.work_counters().selection_dimension_bounds_build_count,
            work.selection_dimension_bounds_build_count);
  previews.front().dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(
      std::vector<QVec3>{{3, 1, 0}, {5, 3, 0}, {4, 2, 1}});
  ++request.tool_preview_overlay_revision;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {3, 0, 0}, {12, 3, 2});
  EXPECT_EQ(provider.build_count, 1);
  request.selection_dimension_preview_meshes = {};
  ++request.tool_preview_overlay_revision;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {-8, 0, 0}, {12, 4, 20});
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     FaceDimensionsProjectOnlySelectedVertexIdsThroughAcceptedDynamicPositions) {
  ViewportOverlayFrame frame;
  frame.lines.push_back({.layer = ViewportOverlayLayer::SelectedFaceEdge,
      .start = {-8, 0, 0}, .end = {8, 4, 0},
      .source = ViewportOverlaySource::ComponentSelection,
      .element = {.object_id = 10, .edge_a = 1, .edge_b = 2, .face_id = 1}});
  StaticSelectionFrameProvider provider(frame);
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.style_selection_context = ViewportOverlaySelectionContext::Face;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Face;
  std::vector previews{dimension_preview(10, 100)};
  request.selection_dimension_preview_meshes = previews;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  expect_dimension_bounds(coordinator.submit(request, settings, target), {2, 1, 0}, {4, 3, 0});
  previews.front().dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(
      std::vector<QVec3>{{3, 2, 1}, {4, 4, 1}, {-100, -100, -100}});
  ++request.tool_preview_overlay_revision;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {3, 2, 1}, {4, 4, 1});
  EXPECT_EQ(provider.build_count, 1);
  request.selection_dimension_preview_meshes = {};
  ++request.tool_preview_overlay_revision;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {-8, 0, 0}, {8, 4, 0});
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     PartialGroupAndInstancePreviewsRetainUntouchedLeavesWithoutOldExtrema) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.group_bounds = {{.entity = 300, .min = {-8, 0, 0}, .max = {15, 4, 20}}};
  scene.selected_mesh_bounds = {{.entity = 400, .min = {-8, 0, 0}, .max = {22, 4, 20}}};
  const std::vector<MeshVertex> untouched_group{{.position = {13, 0, 0}}, {.position = {15, 2, 2}}};
  const std::vector<MeshVertex> untouched_instance{{.position = {20, 0, 0}}, {.position = {22, 2, 2}}};
  std::vector previews{dimension_preview(10, 100), dimension_preview(11, 101)};
  previews.back().visibility_entity = 400;
  const auto &original = previews.front().mesh.geometry().vertices;
  scene.hierarchy_selection_wires = {
      {.entity = 100, .selection_root = 300, .vertices = original},
      {.entity = 200, .selection_root = 300, .vertices = untouched_group}};
  RenderOverlaySelectedObjectSnapshot selected_objects;
  selected_objects.instance_selection_fills = {
      {.occurrence = 400, .definition = 500, .member = 101, .vertices = original},
      {.occurrence = 400, .definition = 500, .member = 102, .vertices = untouched_instance}};
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Group;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  request.selection_dimension_preview_meshes = previews;
  request.selected_mesh_outline_snapshot = &selected_objects;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  expect_dimension_bounds(coordinator.submit(request, settings, target), {2, 0, 0}, {22, 3, 2});
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     RotatedGroupPreviewMeasuresMeshVerticesInsteadOfItsLooseCage) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.group_bounds = {{.entity = 300, .min = {0, 0, 0}, .max = {2, 2, 0}}};
  std::vector previews{dimension_preview(10, 100)};
  previews.front().dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(
      std::vector<QVec3>{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}});
  constexpr float kDiagonal = 0.70710678f;
  previews.front().transform.values = {
      kDiagonal, kDiagonal, 0, 0, -kDiagonal, kDiagonal, 0, 0,
      0, 0, 1, 0, 0, 0, 0, 1};
  scene.hierarchy_selection_wires = {{.entity = 100, .selection_root = 300,
      .vertices = previews.front().mesh.geometry().vertices}};
  const std::vector<RenderToolPreviewGroupMarkerRecord> markers{{
      .source_entity = 300, .local_min = {0, 0, 0}, .local_max = {2, 2, 0},
      .instance_transforms = {previews.front().transform}}};
  const RenderToolPreviewSuppressionSnapshot suppression{.entities = {300, 100}};
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Group;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  request.selection_dimension_preview_meshes = previews;
  request.selection_dimension_preview_groups = markers;
  ViewportOverlayFrame preview_cage;
  preview_cage.lines.push_back({.layer = ViewportOverlayLayer::GroupBounds,
      .start = {-9, -9, -9}, .end = {9, -9, -9},
      .style_role = ViewportOverlayStyleRole::GroupBounds,
      .source = ViewportOverlaySource::Tool});
  request.tool_preview_overlay_frame = &preview_cage;
  request.selection_dimension_preview_suppression = &suppression;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  expect_dimension_bounds(coordinator.submit(request, ViewportSettings::defaults(), target),
                          {-2 * kDiagonal, 0, 0}, {2 * kDiagonal, 2 * kDiagonal, 0});
  EXPECT_FALSE(std::ranges::any_of(target.dynamic_frame.lines, [](const auto &line) {
    return line.source == ViewportOverlaySource::Tool &&
           line.layer == ViewportOverlayLayer::GroupBounds;
  }));
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     MeshPreviewKeepsGroupedCurveBoundsAndUsesAcceptedCurveReplacement) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.group_bounds = {{.entity = 300, .min = {-8, 0, 0}, .max = {30, 4, 20}}};
  std::vector previews{dimension_preview(10, 100)};
  scene.hierarchy_selection_wires = {{.entity = 100, .selection_root = 300,
      .vertices = previews.front().mesh.geometry().vertices}};
  RenderOverlayCurveSnapshot grouped_curve;
  grouped_curve.entity = 200;
  grouped_curve.selection_root = 300;
  grouped_curve.selection_dimension_bounds = ViewportSelectionDimensionBounds{{20, 0, 0}, {30, 2, 2}};
  scene.curves.push_back(grouped_curve);
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = true;
  request.scene_selection_mode = RenderOverlaySelectionMode::Group;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  request.selection_dimension_preview_meshes = previews;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  expect_dimension_bounds(coordinator.submit(request, settings, target), {2, 0, 0}, {30, 3, 2});
  RenderToolPreviewCurveRecord curve_preview;
  curve_preview.source_entity = 200;
  curve_preview.suppress_source = true;
  curve_preview.selection_dimension_bounds = ViewportSelectionDimensionBounds{{5, 1, 0}, {7, 2, 1}};
  const std::array curve_previews{curve_preview};
  request.selection_dimension_preview_curves = curve_previews;
  ++request.tool_preview_overlay_revision;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {2, 1, 0}, {7, 3, 1});
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     ArrayDimensionsIncludeOriginalsAndCopiesWithoutUnrelatedSelection) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  scene.selected_mesh_bounds = {{.entity = 999, .min = {-100, -100, -100}, .max = {100, 100, 100}}};
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.selection_dimensions_visible = false;
  request.array_dimensions_mode = ViewportArrayDimensionsMode::Visible;
  auto preview = dimension_preview(10, 100);
  preview.dynamic_positions = false;
  preview.suppress_source = false;
  preview.mesh.edit_geometry().vertices = {
      {.position = {0, 0, 0}}, {.position = {2, 0, 0}}, {.position = {0, 1, 1}}};
  QMat4 original;
  original.values[12] = 10;
  original.values[13] = 5;
  QMat4 copy = original;
  copy.values[14] = 8;
  preview.instance_transforms = {copy};
  std::array previews{preview};
  RenderSceneSnapshot rendered;
  rendered.meshes.push_back({.entity_id = 100,
      .mesh = {.vertices = previews.front().mesh.geometry().vertices},
      .world_transform = original});
  request.selection_dimension_preview_meshes = previews;
  request.array_dimension_scene_snapshot = &rendered;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  expect_dimension_bounds(coordinator.submit(request, settings, target), {10, 5, 0}, {12, 6, 9});
  const auto initial_work = coordinator.work_counters();
  for (int frame = 0; frame < 8; ++frame) {
    request.array_dimensions_mode = ViewportArrayDimensionsMode::Inactive;
    request.selection_dimensions_visible = true;
    request.scene_selection_mode = RenderOverlaySelectionMode::Object;
    request.viewport_index = 1u;
    expect_dimension_bounds(coordinator.submit(request, settings, target), {-100, -100, -100}, {100, 100, 100});
    request.viewport_index = 0u;
    request.camera.eye.x += 0.5f;
    request.raster_scale = frame % 2 == 0 ? 2.0f : 1.0f;
    request.array_dimensions_mode = ViewportArrayDimensionsMode::Visible;
    expect_dimension_bounds(coordinator.submit(request, settings, target), {10, 5, 0}, {12, 6, 9});
  }
  EXPECT_EQ(coordinator.work_counters().array_dimension_bounds_build_count,
            initial_work.array_dimension_bounds_build_count);
  EXPECT_EQ(coordinator.work_counters().array_dimension_originals_build_count,
            initial_work.array_dimension_originals_build_count);
  previews.front().instance_transforms.front().values[14] = 12;
  ++request.tool_preview_overlay_revision;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {10, 5, 0}, {12, 6, 13});
  EXPECT_EQ(coordinator.work_counters().array_dimension_originals_build_count,
            initial_work.array_dimension_originals_build_count);
  request.array_dimensions_mode = ViewportArrayDimensionsMode::Hidden;
  EXPECT_FALSE(coordinator.submit(request, settings, target).selection_dimension_bounds);
  EXPECT_FALSE(std::ranges::any_of(target.dynamic_frame.lines, [](const auto &line) {
    return line.source == ViewportOverlaySource::SelectionDimensions;
  }));
  request.array_dimensions_mode = ViewportArrayDimensionsMode::Visible;
  request.overlays_visible = false;
  EXPECT_FALSE(coordinator.submit(request, settings, target).selection_dimension_bounds);
  request.overlays_visible = true;
  request.selection_dimension_preview_meshes = {};
  ++request.tool_preview_overlay_revision;
  EXPECT_FALSE(coordinator.submit(request, settings, target).selection_dimension_bounds);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     ArrayDimensionsResolveTheExactInstanceOccurrenceAndAffinePlacements) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.array_dimensions_mode = ViewportArrayDimensionsMode::Visible;
  auto preview = dimension_preview(10, 100);
  preview.dynamic_positions = false;
  preview.suppress_source = false;
  preview.visibility_entity = 200;
  preview.mesh.edit_geometry().vertices = {
      {.position = {0, 0, 0}}, {.position = {2, 0, 0}}, {.position = {0, 1, 1}}};
  QMat4 affine;
  affine.values = {-2, 1, 0, 0, 1, 3, 0, 0, 0, 0, 2, 0, 10, 5, 1, 1};
  QMat4 copy = affine;
  copy.values[12] = 20;
  preview.instance_transforms = {copy};
  std::array previews{preview};
  QMat4 unrelated;
  unrelated.values[12] = 1000;
  RenderSceneSnapshot rendered;
  RenderSceneInstancePrimitiveRecord primitive;
  primitive.member_entity = 100;
  primitive.mesh.vertices = previews.front().mesh.geometry().vertices;
  primitive.member_from_definition.values[12] = 500;
  primitive.occurrences = {{.entity_id = 200, .world_transform = affine},
                          {.entity_id = 201, .world_transform = unrelated}};
  rendered.instance_primitives.push_back(primitive);
  request.selection_dimension_preview_meshes = previews;
  request.array_dimension_scene_snapshot = &rendered;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  const auto settings = ViewportSettings::defaults();
  expect_dimension_bounds(coordinator.submit(request, settings, target), {6, 5, 1}, {21, 8, 3});
  rendered.instance_primitives.front().occurrences.front().world_transform.values[12] = 0;
  ++rendered.transform_revision;
  expect_dimension_bounds(coordinator.submit(request, settings, target), {-4, 5, 1}, {21, 8, 3});
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     SourceReplacingArrayDimensionsMeasureCompoundPreviewWithoutOldPositionOrLooseCages) {
  StaticSelectionFrameProvider provider({});
  RenderOverlaySelectionSnapshot scene;
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  auto request = submission_request(provider, scene, volatile_frame, curve);
  request.array_dimensions_mode = ViewportArrayDimensionsMode::Visible;
  std::array previews{dimension_preview(10, 100)};
  request.selection_dimension_preview_meshes = previews;
  RenderSceneSnapshot rendered;
  rendered.meshes.push_back({.entity_id = 100,
      .mesh = {.vertices = previews.front().mesh.geometry().vertices}});
  request.array_dimension_scene_snapshot = &rendered;
  RenderToolPreviewCurveRecord curve_preview;
  curve_preview.selection_dimension_bounds = ViewportSelectionDimensionBounds{{5, 0, 0}, {7, 4, 1}};
  const std::array curves{curve_preview};
  request.selection_dimension_preview_curves = curves;
  QMat4 light_world;
  light_world.values[14] = 9;
  const std::array<RenderToolPreviewLightRecord, 1> lights{{{.instance_transforms = {light_world}}}};
  request.array_dimension_preview_lights = lights;
  QMat4 group_world;
  group_world.values[12] = -2;
  QMat4 parent_pivot;
  parent_pivot.values[12] = -1000;
  const std::array<RenderToolPreviewGroupMarkerRecord, 2> groups{{{
      .local_min = {-100, -100, -100}, .local_max = {100, 100, 100},
      .instance_transforms = {parent_pivot}}, {.instance_transforms = {group_world}}}};
  request.selection_dimension_preview_groups = groups;
  request.tool_preview_overlay_revision = 1u;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  expect_dimension_bounds(coordinator.submit(request, ViewportSettings::defaults(), target),
                          {-2, 0, 0}, {7, 4, 9});
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     FaceFillPrioritiesRemainPaneLocalAndReuseUnchangedSourceFrames) {
  ViewportOverlayFrame selected;
  selected.triangles.push_back({
      .layer = ViewportOverlayLayer::SelectedVisibleFace,
      .a = {-1, -1, -2}, .b = {1, -1, -2}, .c = {0, 1, -2},
      .style_role = ViewportOverlayStyleRole::SelectionFace,
      .source = ViewportOverlaySource::ComponentSelection});
  StaticSelectionFrameProvider selection(selected);
  StaticDynamicToolFrameProvider tools({});
  std::array<ViewportOverlayFrame, 2> hover;
  hover[0].triangles.push_back(selected.triangles.front());
  hover[0].triangles.front().layer = ViewportOverlayLayer::HoverFace;
  hover[0].triangles.front().style_role = ViewportOverlayStyleRole::HoverFace;
  hover[0].triangles.front().source = ViewportOverlaySource::Hover;
  std::array<std::uint64_t, 2> revisions{1u, 1u};
  RenderOverlaySelectionSnapshot scene;
  CurveToolPreviewState curve;
  ViewportOverlaySubmissionCoordinator coordinator;
  std::array<RecordingOverlayTarget, 2> targets;
  auto settings = ViewportSettings::defaults();
  for (const auto color : {ViewportColorSettingId::SelectionFace,
                           ViewportColorSettingId::HoverFace,
                           ViewportColorSettingId::RemoveSelectionFace}) {
    ASSERT_TRUE(settings.set_color(color, {0.25f, 0.5f, 0.75f, 0.3f}));
  }
  const auto submit = [&](std::size_t pane) {
    auto request = submission_request(selection, scene, hover[pane], curve);
    request.viewport_index = pane;
    request.camera.projection = ViewportOverlayProjection::Perspective;
    request.selection_source.selection_context = ViewportOverlaySelectionContext::Face;
    request.style_selection_context = ViewportOverlaySelectionContext::Face;
    request.volatile_modeling_source.publication_revision = revisions[pane];
    request.dynamic_tool_frame_provider = &tools;
    request.dynamic_tool_source = {
        .source_identity = reinterpret_cast<std::uintptr_t>(&tools),
        .publication_revision = 1u};
    return coordinator.submit(request, settings, targets[pane]);
  };
  for (std::size_t pane = 0u; pane < targets.size(); ++pane) {
    ASSERT_TRUE(submit(pane).persistent_submitted);
    const auto plan = OverlayRenderableBuilder::build_layer(
        targets[pane].persistent_frame, {}, ViewportOverlayLayer::SelectedVisibleFace);
    ASSERT_EQ(plan.face_fill_batches.size(), 2u);
    for (const auto &batch : plan.face_fill_batches) {
      EXPECT_EQ(batch.state.face_fill_class, OverlayFaceFillClass::Selection);
    }
  }
  ASSERT_EQ(targets[0].dynamic_frame.triangles.size(), 1u);
  ASSERT_EQ(targets[0].persistent_frame.triangles.size(), 1u);
  EXPECT_EQ(targets[0].dynamic_frame.triangles.front().color,
            targets[0].persistent_frame.triangles.front().color);
  EXPECT_TRUE(targets[1].dynamic_frame.triangles.empty());
  const auto builds = selection.build_count;
  const auto work = coordinator.work_counters();
  for (int frame = 0; frame < 8; ++frame) {
    for (std::size_t pane = 0u; pane < targets.size(); ++pane) {
      EXPECT_TRUE(submit(pane).camera_only);
    }
  }
  EXPECT_EQ(selection.build_count, builds);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            work.dynamic_source_compose_count);

  const auto old_color = targets[0].dynamic_frame.triangles.front().color;
  hover[0].triangles.front().style_role = ViewportOverlayStyleRole::RemoveSelectionFace;
  ++revisions[0];
  const auto changed = submit(0u);
  EXPECT_TRUE(changed.dynamic_submitted);
  EXPECT_FALSE(changed.persistent_submitted);
  EXPECT_EQ(targets[0].dynamic_frame.triangles.front().color, old_color);
  const auto remove_plan = OverlayRenderableBuilder::build_layer(
      targets[0].dynamic_frame, {}, ViewportOverlayLayer::HoverFace);
  ASSERT_EQ(remove_plan.face_fill_batches.size(), 2u);
  for (const auto &batch : remove_plan.face_fill_batches) {
    EXPECT_EQ(batch.state.face_fill_class, OverlayFaceFillClass::RemoveSelection);
  }
  EXPECT_TRUE(submit(1u).camera_only);
  EXPECT_TRUE(targets[1].dynamic_frame.triangles.empty());
  EXPECT_EQ(selection.build_count, builds);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
            work.dynamic_source_compose_count + 1u);
}

TEST(ViewportOverlaySubmissionCoordinatorTests,
     GroupActiveOnlyAndColorOnlyChangesPublishWhileUnchangedFramesReuseSources) {
  SceneDocument document;
  AssetRegistry assets;
  MeshAsset mesh;
  mesh.edit_geometry().vertices = {MeshVertex{.position = {0, 0, 0}},
      MeshVertex{.position = {1, 0, 0}}, MeshVertex{.position = {0, 1, 0}}};
  mesh.edit_geometry().indices = {0, 1, 2};
  const auto mesh_id = assets.add_mesh(std::move(mesh));
  const auto first = document.create_empty("First");
  const auto second = document.create_empty("Second");
  for (const auto group : {first, second}) {
    const auto child = document.create_entity("Child", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(child, {.mesh = mesh_id}));
    ASSERT_TRUE(document.set_parent(child, group));
    ASSERT_TRUE(document.set_selected(group, true));
  }
  ASSERT_TRUE(document.set_transform(second, TransformComponent{.position = {4, 0, 0}}));
  RenderOverlaySnapshotCache snapshots;
  ViewportOverlaySubmissionCoordinator coordinator;
  RecordingOverlayTarget target;
  StaticSelectionFrameProvider provider({});
  ViewportOverlayFrame volatile_frame;
  CurveToolPreviewState curve;
  ViewportSettings settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::BoundingBoxWire,
      {0.2f, 0.4f, 0.6f, 0.4f}));
  const auto submit = [&](float scale) {
    const auto &scene = snapshots.selection_snapshot(document, assets);
    auto request = submission_request(provider, scene, volatile_frame, curve);
    request.scene_selection_mode = RenderOverlaySelectionMode::Group;
    request.style_selection_context = ViewportOverlaySelectionContext::Group;
    request.selection_dimensions_visible = true;
    request.raster_scale = scale;
    return coordinator.submit(request, settings, target);
  };
  const auto expect_active_side = [&](bool right, std::uint32_t color) {
    std::size_t active = 0u;
    std::size_t ordinary = 0u;
    for (const auto &line : target.persistent_frame.lines) {
      if (line.source != ViewportOverlaySource::GroupBounds) continue;
      if (line.style_role == ViewportOverlayStyleRole::ActiveGroupBounds) {
        ++active;
        EXPECT_EQ(line.color, color);
        EXPECT_EQ(line.start.x >= 4.0f, right);
      } else {
        ++ordinary;
        EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::GroupBounds);
      }
    }
    EXPECT_EQ(active, 12u);
    EXPECT_EQ(ordinary, 12u);
  };
  ASSERT_TRUE(submit(1.0f).persistent_submitted);
  expect_active_side(true, rgba8(102, 140, 179, 102));
  const auto builds = snapshots.stats().selection_build_count;
  EXPECT_FALSE(submit(1.0f).persistent_submitted);
  EXPECT_EQ(snapshots.stats().selection_build_count, builds);
  ASSERT_TRUE(document.set_active_selection(first));
  ASSERT_TRUE(submit(1.0f).persistent_submitted);
  expect_active_side(false, rgba8(102, 140, 179, 102));
  EXPECT_EQ(document.selected_entities().size(), 2u);
  const auto active_builds = snapshots.stats().selection_build_count;
  ASSERT_TRUE(settings.set_color(ViewportColorSettingId::BoundingBoxWire,
      {1, 1, 1, 0.4f}));
  ASSERT_TRUE(submit(2.0f).persistent_submitted);
  expect_active_side(false, rgba8(255, 255, 255, 102));
  EXPECT_EQ(snapshots.stats().selection_build_count, active_builds);
  EXPECT_FALSE(submit(2.0f).persistent_submitted);
  EXPECT_EQ(snapshots.stats().selection_build_count, active_builds);
}

} // namespace
} // namespace quader

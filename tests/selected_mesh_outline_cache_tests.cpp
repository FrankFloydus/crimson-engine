////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_mesh_outline_cache.hpp"
#include "render/overlay/instance_selection_overlay_cache.hpp"
#include "render/overlay/scene_wireframe_overlay_cache.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "assets/asset_registry.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/viewport_scene_projection.hpp"
#include "math/glm_bridge.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

void create_shape(SceneSession &session, PrimitiveCreationKind kind) {
  auto &primitive = session.primitive_creation();
  ASSERT_TRUE(primitive.activate(kind).ok);
  if (kind == PrimitiveCreationKind::Box) {
    for (auto setting : {kBoxSubdivisionsXSetting, kBoxSubdivisionsYSetting, kBoxSubdivisionsZSetting}) {
      ASSERT_TRUE(primitive.set_setting(setting, 5).ok);
    }
  }
  ModelingBoxPreview preview;
  preview.ok = preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = {QVec3{-1, 0, -1}, QVec3{-1, 0, 1}, QVec3{1, 0, 1},
      QVec3{1, 0, -1}, QVec3{-1, 2, -1}, QVec3{-1, 2, 1},
      QVec3{1, 2, 1}, QVec3{1, 2, -1}};
  preview.corner_count = preview.corners.size();
  ASSERT_TRUE(primitive.accept_preview(preview, "Outline").ok);
}

ViewportOverlayCamera perspective() {
  ViewportOverlayCamera camera;
  camera.eye = {5, 6, 8};
  camera.forward = normalize(QVec3{0, 1, 0} - camera.eye);
  camera.right = normalize(cross(camera.forward, QVec3{0, 1, 0}));
  camera.up = cross(camera.right, camera.forward);
  camera.viewport_size = {800, 600};
  return camera;
}

/// Supplies an independent ray/slab occupancy oracle for the selected box fixtures.
struct OutlineBox {
  QVec3 min, max;

  std::array<QVec3, 8> corners() const {
    return {{{min.x, min.y, min.z}, {max.x, min.y, min.z},
             {max.x, max.y, min.z}, {min.x, max.y, min.z},
             {min.x, min.y, max.z}, {max.x, min.y, max.z},
             {max.x, max.y, max.z}, {min.x, max.y, max.z}}};
  }

  bool intersects(QVec3 origin, QVec3 direction, float near_plane) const {
    float enter = near_plane, leave = std::numeric_limits<float>::infinity();
    const std::array<float, 3> start{origin.x, origin.y, origin.z};
    const std::array<float, 3> delta{direction.x, direction.y, direction.z};
    const std::array<float, 3> low{min.x, min.y, min.z}, high{max.x, max.y, max.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (std::abs(delta[axis]) < 1e-8f) {
        if (start[axis] < low[axis] || start[axis] > high[axis]) return false;
        continue;
      }
      const float a = (low[axis] - start[axis]) / delta[axis];
      const float b = (high[axis] - start[axis]) / delta[axis];
      enter = std::max(enter, std::min(a, b));
      leave = std::min(leave, std::max(a, b));
    }
    return enter <= leave;
  }
};

float screen_length(QVec2 value) { return std::hypot(value.x, value.y); }

QVec2 project_outline_point(QVec3 point, const ViewportOverlayCamera &camera) {
  const QVec3 relative = point - camera.eye;
  const float depth = camera.projection == ViewportOverlayProjection::Perspective
      ? dot(relative, camera.forward) : 1.0f;
  return {dot(relative, camera.right) / depth, dot(relative, camera.up) / depth};
}

// Probe rays either side of every output segment, then find independent occupancy
// transitions across the fixture. This rejects interior wires AND missing silhouette pieces.
void expect_box_union_silhouette(const ViewportOverlayFrame &frame,
    const ViewportOverlayCamera &camera, std::span<const OutlineBox> boxes) {
  ASSERT_FALSE(frame.lines.empty());
  std::vector<std::array<QVec2, 2>> segments;
  QVec2 low{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
  QVec2 high{-low.x, -low.y};
  for (const auto &box : boxes) {
    for (const auto corner : box.corners()) {
      const auto point = project_outline_point(corner, camera);
      low.x = std::min(low.x, point.x);
      low.y = std::min(low.y, point.y);
      high.x = std::max(high.x, point.x);
      high.y = std::max(high.y, point.y);
    }
  }
  const float tolerance = std::max(high.x - low.x, high.y - low.y) * 1e-5f;
  const auto covered = [&](QVec2 point) {
    const QVec3 offset = camera.right * point.x + camera.up * point.y;
    const bool perspective_view = camera.projection == ViewportOverlayProjection::Perspective;
    const QVec3 origin = camera.eye + (perspective_view ? QVec3{} : offset);
    const QVec3 direction = camera.forward + (perspective_view ? offset : QVec3{});
    return std::ranges::any_of(boxes, [&](const auto &box) {
      return box.intersects(origin, direction, camera.near_plane);
    });
  };
  for (const auto &line : frame.lines) {
    EXPECT_EQ(line.source, ViewportOverlaySource::MeshOutline);
    const QVec2 a = project_outline_point(line.start, camera);
    const QVec2 b = project_outline_point(line.end, camera);
    const QVec2 delta = b - a;
    const float size = screen_length(delta);
    ASSERT_GT(size, tolerance * 0.01f);
    segments.push_back({a, b});
    const QVec2 normal{-delta.y / size * tolerance, delta.x / size * tolerance};
    for (const float t : {0.03125f, 0.125f, 0.25f, 0.5f, 0.75f, 0.875f, 0.96875f}) {
      const QVec2 point = a + delta * t;
      EXPECT_NE(covered(point + normal), covered(point - normal))
          << "Interior or detached outline at " << point.x << ", " << point.y;
    }
  }
  const auto distance_to_outline = [&](QVec2 point) {
    float distance = std::numeric_limits<float>::infinity();
    for (const auto &segment : segments) {
      const QVec2 delta = segment[1] - segment[0], relative = point - segment[0];
      const float t = std::clamp((relative.x * delta.x + relative.y * delta.y) /
          (delta.x * delta.x + delta.y * delta.y), 0.0f, 1.0f);
      distance = std::min(distance, screen_length(point - (segment[0] + delta * t)));
    }
    return distance;
  };
  int transitions = 0;
  for (const bool vertical : {false, true}) {
    for (int row = 1; row < 24; ++row) {
      const auto point_at = [&](float t) {
        return vertical
            ? QVec2{low.x + (high.x - low.x) * row / 24, low.y + (high.y - low.y) * t}
            : QVec2{low.x + (high.x - low.x) * t, low.y + (high.y - low.y) * row / 24};
      };
      QVec2 previous = point_at(-0.05f);
      bool previous_covered = covered(previous);
      for (int column = 1; column <= 128; ++column) {
        const QVec2 next = point_at(-0.05f + 1.1f * column / 128);
        const bool next_covered = covered(next);
        if (next_covered != previous_covered) {
          QVec2 a = previous, b = next;
          for (int step = 0; step < 14; ++step) {
            const QVec2 middle = (a + b) * 0.5f;
            if (covered(middle) == previous_covered) a = middle;
            else b = middle;
          }
          EXPECT_LE(distance_to_outline((a + b) * 0.5f), tolerance * 4)
              << "Missing projected silhouette boundary";
          ++transitions;
        }
        previous = next;
        previous_covered = next_covered;
      }
    }
  }
  EXPECT_GT(transitions, 20);
}

void append_outline_box(MeshGeometryPayload &geometry, const OutlineBox &box) {
  const auto offset = static_cast<std::uint32_t>(geometry.vertices.size());
  for (const auto point : box.corners()) geometry.vertices.push_back({.position = point});
  for (const auto index : {0u, 2u, 1u, 0u, 3u, 2u, 4u, 5u, 6u, 4u, 6u, 7u,
                          0u, 1u, 5u, 0u, 5u, 4u, 3u, 7u, 6u, 3u, 6u, 2u,
                          0u, 4u, 7u, 0u, 7u, 3u, 1u, 2u, 6u, 1u, 6u, 5u}) {
    geometry.indices.push_back(offset + index);
  }
}

void seed_outline_boxes(SceneSession &session, std::span<const OutlineBox> boxes, bool grouped) {
  auto &document = session.scene();
  const auto group = grouped ? document.create_entity("Selected assembly", EntityKind::Empty)
                             : kInvalidEntityId;
  const auto add = [&](std::span<const OutlineBox> members) {
    MeshAsset asset;
    asset.name = "Outline assembly";
    auto &geometry = asset.edit_geometry();
    for (const auto &box : members) append_outline_box(geometry, box);
    geometry.primitives.push_back({.index_count = static_cast<std::uint32_t>(geometry.indices.size())});
    const auto mesh = session.assets().add_mesh(std::move(asset));
    const auto entity = document.create_entity("Box shells", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(entity, MeshComponent{.mesh = mesh}));
    if (grouped) ASSERT_TRUE(document.set_parent(entity, group, false));
    else ASSERT_TRUE(document.set_selected(entity, true));
  };
  if (grouped) {
    for (std::size_t i = 0; i < boxes.size(); ++i) add(boxes.subspan(i, 1));
    ASSERT_TRUE(document.set_selected(group, true));
  } else {
    add(boxes);
  }
}

TEST(SelectedMeshOutlineCacheTests, SubdividedBoxHasOnlySilhouetteWithComplementaryDepthPasses) {
  SceneSession session;
  create_shape(session, PrimitiveCreationKind::Box);
  auto snapshot = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  SelectedMeshOutlineCache cache;
  const auto camera = perspective();
  const auto &frame = cache.frame(&snapshot, camera, 0);
  ASSERT_FALSE(frame.lines.empty());
  EXPECT_LT(frame.lines.size(), snapshot.meshes.front().wire_segments.size());
  float perimeter = 0;
  for (const auto &line : frame.lines) {
    perimeter += length(line.end - line.start);
    EXPECT_EQ(line.source, ViewportOverlaySource::MeshOutline);
    EXPECT_EQ(viewport_overlay_depth_mode(line), ViewportOverlayDepthMode::DepthTested);
    const QVec3 midpoint = (line.start + line.end) * 0.5f;
    const int boundary_axes = static_cast<int>(std::abs(std::abs(midpoint.x) - 1) < 0.0001f) +
        static_cast<int>(std::abs(std::abs(midpoint.y - 1) - 1) < 0.0001f) +
        static_cast<int>(std::abs(std::abs(midpoint.z) - 1) < 0.0001f);
    EXPECT_GE(boundary_axes, 2);
  }
  EXPECT_NEAR(perimeter, 12, 0.001f);
  const auto plan = OverlayRenderableBuilder::build_layer(frame, camera, ViewportOverlayLayer::SelectedEdge);
  ASSERT_FALSE(plan.line_batches.empty());
  for (const auto &batch : plan.line_batches) {
    EXPECT_TRUE(batch.state.depth_culling);
    EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
    ASSERT_TRUE(batch.occluded_state.has_value());
    EXPECT_TRUE(batch.occluded_state->depth_culling);
    EXPECT_EQ(batch.occluded_state->depth_func, OverlayDepthFunc::Less);
    EXPECT_FALSE(batch.occluded_state->depth_write);
    EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::Visible);
    EXPECT_EQ(batch.occluded_state->silhouette_pass,
              OverlaySilhouettePass::Occluded);
    EXPECT_LT(batch.state.blend_order, batch.occluded_state->blend_order);
    EXPECT_FLOAT_EQ(batch.occluded_alpha_multiplier, 0.5f);
  }
}

TEST(SelectedMeshOutlineCacheTests, RoundMeshesDiscardFacetWiresAndRespectCameraAndGeometryRevisions) {
  for (auto kind : {PrimitiveCreationKind::Cylinder, PrimitiveCreationKind::Cone,
                   PrimitiveCreationKind::Sphere, PrimitiveCreationKind::Torus}) {
    SCOPED_TRACE(static_cast<int>(kind));
    SceneSession session;
    create_shape(session, kind);
    auto snapshot = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
    SelectedMeshOutlineCache cache;
    auto camera = perspective();
    const auto original = cache.frame(&snapshot, camera, 0).lines.size();
    ASSERT_GT(original, 0u);
    EXPECT_LT(original, snapshot.meshes.front().wire_segments.size());
    const auto work = cache.work_stats();
    const auto revision = cache.revision(0);
    for (int i = 0; i < 30; ++i) (void)cache.frame(&snapshot, camera, 0);
    EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
    EXPECT_EQ(cache.work_stats().occurrence_bounds_tests, work.occurrence_bounds_tests);
    EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
    EXPECT_EQ(cache.revision(0), revision);
    camera.fov_degrees = 40;
    camera.viewport_size = {1600, 900};
    (void)cache.frame(&snapshot, camera, 0);
    EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
    camera.eye.x = -5;
    camera.fov_degrees = 130;
    EXPECT_FALSE(cache.frame(&snapshot, camera, 0).lines.empty());
    EXPECT_GT(cache.revision(0), revision);
    EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
    const auto moved_work = cache.work_stats();
    camera.forward = normalize(QVec3{0, 1, 0} - camera.eye);
    camera.right = normalize(cross(camera.forward, QVec3{0, 1, 0}));
    camera.up = cross(camera.right, camera.forward);
    (void)cache.frame(&snapshot, camera, 0);
    EXPECT_EQ(cache.work_stats().camera_classifications, moved_work.camera_classifications + 1);
    camera.near_plane = 0.25f;
    (void)cache.frame(&snapshot, camera, 0);
    EXPECT_EQ(cache.work_stats().camera_classifications, moved_work.camera_classifications + 2);
    EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
    camera.projection = ViewportOverlayProjection::Orthographic;
    camera.forward = {0, 0, -1};
    camera.right = {1, 0, 0};
    camera.up = {0, 1, 0};
    EXPECT_FALSE(cache.frame(&snapshot, camera, 1).lines.empty());
    const auto ortho_work = cache.work_stats();
    camera.eye = {1, 2, 300};
    camera.orthographic_size = 100;
    camera.fov_degrees = 75;
    (void)cache.frame(&snapshot, camera, 1);
    EXPECT_EQ(cache.work_stats().camera_classifications, ortho_work.camera_classifications);
    EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
    camera.eye = {100, 200, 300};
    EXPECT_TRUE(cache.frame(&snapshot, camera, 1).lines.empty());
    EXPECT_EQ(cache.work_stats().camera_classifications, ortho_work.camera_classifications + 1);
    camera.eye = {0, 1, 20};
    EXPECT_FALSE(cache.frame(&snapshot, camera, 1).lines.empty());
    EXPECT_EQ(cache.work_stats().camera_classifications, ortho_work.camera_classifications + 2);
    ASSERT_TRUE(session.primitive_creation().set_setting(kPrimitiveFlipFacesSetting, 1).ok);
    snapshot = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
    EXPECT_FALSE(cache.frame(&snapshot, camera, 0).lines.empty());
    EXPECT_GT(cache.work_stats().topology_builds, work.topology_builds);
    EXPECT_TRUE(cache.frame(nullptr, camera, 0).lines.empty());
  }
}

TEST(SelectedMeshOutlineCacheTests, OpenMeshWeldsSplitCornersAndSharesTopologyAcrossMirroredOccurrences) {
  auto payload = std::make_shared<MeshGeometryPayload>();
  payload->vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{1, 1, 0}},
                       {{-1, -1, 0}}, {{1, 1, 0}}, {{-1, 1, 0}}};
  payload->indices = {0, 1, 2, 3, 4, 5};
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.meshes.push_back({.geometry_payload = payload, .selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  SelectedMeshOutlineCache cache;
  auto camera = perspective();
  camera.projection = ViewportOverlayProjection::Orthographic;
  camera.eye = {0, 0, 10};
  camera.forward = {0, 0, -1};
  camera.right = {1, 0, 0};
  camera.up = {0, 1, 0};
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  snapshot.meshes.push_back(snapshot.meshes.front());
  snapshot.meshes.back().world_transform = compose_transform(
      {.location = {4, 0, 0}, .rotation = {0, 0.3826834f, 0, 0.9238795f}, .scale = {-2, 3, 1}});
  ++snapshot.selection_revision;
  const auto &frame = cache.frame(&snapshot, camera, 0);
  EXPECT_EQ(frame.lines.size(), 8u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  ++snapshot.transform_revision;
  snapshot.meshes.back().world_transform = compose_transform({.location = {8, 0, 0}});
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 8u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  cache.reset();
  EXPECT_TRUE(cache.frame(nullptr, camera, 0).lines.empty());
}

std::shared_ptr<MeshGeometryPayload> outline_quad() {
  auto payload = std::make_shared<MeshGeometryPayload>();
  payload->vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{1, 1, 0}}, {{-1, 1, 0}}};
  payload->indices = {0, 1, 2, 0, 2, 3};
  return payload;
}

ViewportOverlayCamera front_orthographic() {
  auto camera = perspective();
  camera.projection = ViewportOverlayProjection::Orthographic;
  camera.eye = {0, 0, 10};
  camera.forward = {0, 0, -1};
  camera.right = {1, 0, 0};
  camera.up = {0, 1, 0};
  return camera;
}

TEST(SelectedMeshOutlineCacheTests, FramingChangesRetainOrRestoreVisibleOccurrences) {
  const auto payload = outline_quad();
  RenderOverlaySelectedObjectSnapshot snapshot;
  for (const float x : {0.0f, 10.0f}) {
    snapshot.meshes.push_back({.geometry_payload = payload,
        .world_transform = compose_transform({.location = {x, 0, 0}}), .selected = true,
        .vertices = payload->vertices, .indices = payload->indices});
  }
  auto camera = front_orthographic();
  camera.viewport_size = {400, 400};
  camera.orthographic_size = 4;
  SelectedMeshOutlineCache cache;
  ASSERT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  const auto work = cache.work_stats();
  for (int repeat = 0; repeat < 30; ++repeat) (void)cache.frame(&snapshot, camera, 0);
  EXPECT_EQ(cache.work_stats().occurrence_bounds_tests, work.occurrence_bounds_tests);
  EXPECT_EQ(cache.work_stats().visibility_classifications, work.visibility_classifications);
  EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
  camera.orthographic_size = 6;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
  EXPECT_EQ(cache.work_stats().visibility_classifications, work.visibility_classifications + 1);
  camera.eye.x = 10;
  const auto &moved = cache.frame(&snapshot, camera, 0);
  ASSERT_EQ(moved.lines.size(), 4u);
  for (const auto &line : moved.lines) EXPECT_GE(line.start.x, 9);
  camera.eye.x = 20;
  EXPECT_TRUE(cache.frame(&snapshot, camera, 0).lines.empty());
  camera.eye.x = 0;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  camera.projection = ViewportOverlayProjection::Perspective;
  camera.fov_degrees = 30;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  camera.fov_degrees = 100;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 8u);
  camera.fov_degrees = 60;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  camera.viewport_size.x = 800;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 8u);
  camera.right = {0, 1, 0};
  camera.up = {-1, 0, 0};
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  camera.right = {1, 0, 0};
  camera.up = {0, 1, 0};
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 8u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  EXPECT_EQ(cache.work_stats().source_refreshes, work.source_refreshes);
}

TEST(SelectedMeshOutlineCacheTests, FrustumRetainsViewportStraddlingOccluders) {
  const std::array<OutlineBox, 2> boxes{{
      {{-20, -20, -1}, {20, 20, 1}}, {{-1, -1, 3}, {1, 1, 4}},
  }};
  SceneSession session;
  ASSERT_NO_FATAL_FAILURE(seed_outline_boxes(session, boxes, true));
  auto selected = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
  auto hierarchy = build_render_overlay_selection_snapshot(session.scene(), session.assets());
  SelectedMeshOutlineCache cache;
  auto camera = front_orthographic();
  camera.viewport_size = {400, 400};
  camera.orthographic_size = 4;
  for (const auto projection : {ViewportOverlayProjection::Orthographic,
                                ViewportOverlayProjection::Perspective}) {
    camera.projection = projection;
    const auto &frame = cache.frame(&selected, camera, 0, &hierarchy);
    ASSERT_NO_FATAL_FAILURE(expect_box_union_silhouette(frame, camera, boxes));
    for (const auto &line : frame.lines) {
      EXPECT_LE(line.start.z, 1);
      EXPECT_LE(line.end.z, 1);
    }
  }
}

TEST(SelectedMeshOutlineCacheTests, FrustumRetainsMirroredNearClippedGeometry) {
  const auto payload = outline_quad();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.meshes.push_back({.geometry_payload = payload,
      .world_transform = compose_transform({.rotation = {0, 0.3826834f, 0, 0.9238795f},
                                           .scale = {-2, 3, 1}}), .selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  auto camera = front_orthographic();
  camera.projection = ViewportOverlayProjection::Perspective;
  camera.eye = {0, 0, 2};
  camera.near_plane = 2;
  camera.viewport_size = {400, 400};
  SelectedMeshOutlineCache cache;
  const auto &frame = cache.frame(&snapshot, camera, 0);
  ASSERT_EQ(frame.lines.size(), 3u);
  for (const auto &line : frame.lines) {
    EXPECT_GE(dot(line.start - camera.eye, camera.forward), camera.near_plane - 1e-5f);
    EXPECT_GE(dot(line.end - camera.eye, camera.forward), camera.near_plane - 1e-5f);
  }
  camera.eye.x = 100;
  EXPECT_TRUE(cache.frame(&snapshot, camera, 0).lines.empty());
  camera.eye.x = 0;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 3u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
}

TEST(SelectedMeshOutlineCacheTests, FrustumMatchesMinimumOrthographicRendererExtent) {
  const auto payload = outline_quad();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.meshes.push_back({.geometry_payload = payload,
      .world_transform = compose_transform({.location = {0.000075f, 0, 0},
                                           .scale = {0.000003f, 0.000003f, 1}}), .selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  auto camera = front_orthographic();
  camera.viewport_size = {400, 400};
  SelectedMeshOutlineCache cache;
  for (const float size : {0.0f, 0.0001f, 0.00015f}) {
    camera.orthographic_size = size;
    EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  }
}

TEST(SelectedMeshOutlineCacheTests, ActiveObjectKeepsSilhouetteInsideOtherSelectedCoverage) {
  const auto payload = outline_quad();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.meshes.push_back({.geometry_payload = payload,
      .world_transform = compose_transform({.scale = {3, 3, 1}}), .selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  snapshot.meshes.push_back({.geometry_payload = payload,
      .world_transform = compose_transform({.location = {0, 0, 1}}),
      .selected = true, .active_selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  SelectedMeshOutlineCache cache;
  const auto &frame = cache.frame(&snapshot, front_orthographic(), 0);
  ASSERT_EQ(frame.lines.size(), 8u);
  std::size_t active_edges = 0;
  for (const auto &line : frame.lines) {
    EXPECT_EQ(viewport_overlay_depth_mode(line), ViewportOverlayDepthMode::DepthTested);
    if (line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire) {
      ++active_edges;
      EXPECT_FLOAT_EQ(line.start.z, 1);
      EXPECT_FLOAT_EQ(line.end.z, 1);
    } else {
      EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::SelectionWire);
      EXPECT_FLOAT_EQ(line.start.z, 0);
      EXPECT_FLOAT_EQ(line.end.z, 0);
    }
  }
  EXPECT_EQ(active_edges, 4u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
}

TEST(SelectedMeshOutlineCacheTests, ActiveCoincidentOutlineWinsIndependentlyOfSourceOrder) {
  const auto payload = outline_quad();
  for (const bool active_first : {false, true}) {
    SCOPED_TRACE(active_first);
    RenderOverlaySelectedObjectSnapshot snapshot;
    snapshot.meshes.push_back({.geometry_payload = payload, .selected = true,
        .active_selected = active_first, .vertices = payload->vertices, .indices = payload->indices});
    snapshot.meshes.push_back({.geometry_payload = payload, .selected = true,
        .active_selected = !active_first, .vertices = payload->vertices, .indices = payload->indices});
    SelectedMeshOutlineCache cache;
    const auto &frame = cache.frame(&snapshot, front_orthographic(), 0);
    ASSERT_EQ(frame.lines.size(), 4u);
    for (const auto &line : frame.lines) {
      EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::ActiveSelectionWire);
    }
    EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  }
}

TEST(SelectedMeshOutlineCacheTests, ActivePartialOverlapSplitsOrdinaryBoundary) {
  const auto payload = outline_quad();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.meshes.push_back({.geometry_payload = payload,
      .world_transform = compose_transform({.scale = {2, 2, 1}}), .selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  snapshot.meshes.push_back({.geometry_payload = payload,
      .world_transform = compose_transform({.location = {0, 1, 0}}),
      .selected = true, .active_selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  SelectedMeshOutlineCache cache;
  const auto &frame = cache.frame(&snapshot, front_orthographic(), 0);
  ASSERT_EQ(frame.lines.size(), 9u);
  float ordinary_length = 0, active_length = 0;
  std::size_t ordinary_top_fragments = 0;
  for (const auto &line : frame.lines) {
    if (line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire) {
      active_length += length(line.end - line.start);
    } else {
      ordinary_length += length(line.end - line.start);
      if (line.start.y == 2 && line.end.y == 2) {
        ++ordinary_top_fragments;
        EXPECT_TRUE(line.end.x <= -1 || line.start.x >= 1);
      }
    }
  }
  EXPECT_EQ(ordinary_top_fragments, 2u);
  EXPECT_NEAR(ordinary_length, 14, 0.0001f);
  EXPECT_NEAR(active_length, 8, 0.0001f);
}

TEST(SelectedMeshOutlineCacheTests, ActiveOnlySwapPublishesStyleWithoutRebuildingTopology) {
  const auto payload = outline_quad();
  RenderOverlaySelectedObjectSnapshot snapshot;
  for (const float x : {-3.0f, 3.0f}) {
    snapshot.meshes.push_back({.geometry_payload = payload,
        .world_transform = compose_transform({.location = {x, 0, 0}}), .selected = true,
        .active_selected = x < 0, .vertices = payload->vertices, .indices = payload->indices});
  }
  SelectedMeshOutlineCache cache;
  const auto camera = front_orthographic();
  ASSERT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 8u);
  const auto revision = cache.revision(0);
  const auto before = cache.work_stats();
  snapshot.meshes[0].active_selected = false;
  snapshot.meshes[1].active_selected = true;
  ++snapshot.selection_revision;
  const auto &frame = cache.frame(&snapshot, camera, 0);
  ASSERT_EQ(frame.lines.size(), 8u);
  EXPECT_GT(cache.revision(0), revision);
  for (const auto &line : frame.lines) {
    EXPECT_EQ(line.style_role, line.start.x > 0
        ? ViewportOverlayStyleRole::ActiveSelectionWire : ViewportOverlayStyleRole::SelectionWire);
  }
  EXPECT_EQ(cache.work_stats().topology_builds, before.topology_builds);
  EXPECT_EQ(cache.work_stats().source_refreshes, before.source_refreshes + 1);
  EXPECT_EQ(cache.work_stats().camera_classifications, before.camera_classifications + 1);
  const auto after = cache.work_stats();
  const auto active_revision = cache.revision(0);
  for (int i = 0; i < 20; ++i) (void)cache.frame(&snapshot, camera, 0);
  EXPECT_EQ(cache.work_stats().topology_builds, after.topology_builds);
  EXPECT_EQ(cache.work_stats().source_refreshes, after.source_refreshes);
  EXPECT_EQ(cache.work_stats().camera_classifications, after.camera_classifications);
  EXPECT_EQ(cache.revision(0), active_revision);
}

std::shared_ptr<MeshWireGeometryPayload> outline_quad_wires() {
  auto wires = std::make_shared<MeshWireGeometryPayload>();
  wires->content_id = 77;
  wires->segments = {{{-1, -1, 0}, {1, -1, 0}}, {{1, -1, 0}, {1, 1, 0}},
                    {{1, 1, 0}, {-1, 1, 0}}, {{-1, 1, 0}, {-1, -1, 0}}};
  return wires;
}

TEST(SelectedMeshOutlineCacheTests, PersistentSceneWiresPartitionActiveColorAndInvalidateActiveOnlyInputs) {
  const auto payload = outline_quad();
  const auto wires = outline_quad_wires();
  RenderOverlaySceneWireframeSnapshot snapshot;
  snapshot.style.selection_wire_color = rgba8(64, 96, 128, 91);
  snapshot.style.active_selection_wire_color =
      viewport_active_selection_color(snapshot.style.selection_wire_color);
  for (const EntityId id : {EntityId{1}, EntityId{2}}) {
    snapshot.meshes.push_back({.entity = id, .geometry_payload = payload,
        .wire_geometry_payload = wires, .selected = true, .active_selected = id == 1,
        .vertices = payload->vertices, .indices = payload->indices, .wire_segments = wires->segments});
  }
  ASSERT_TRUE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, snapshot.meshes));
  const auto before = plan_scene_wireframe_overlay_batches(snapshot, 64);
  ASSERT_EQ(before.batches.chunks.size(), 2u);
  for (const auto &chunk : before.batches.chunks) {
    ASSERT_EQ(chunk.occurrences.size(), 1u);
    EXPECT_EQ(chunk.key.index_offset, chunk.occurrences.front() == 1
        ? snapshot.style.active_selection_wire_color : snapshot.style.selection_wire_color);
    EXPECT_EQ(chunk.key.geometry_payload, wires->content_id);
    EXPECT_EQ(chunk.key.index_offset >> 24u, 91u);
  }
  auto swapped = snapshot;
  swapped.meshes[0].active_selected = false;
  swapped.meshes[1].active_selected = true;
  EXPECT_FALSE(scene_wireframe_overlay_mesh_inputs_match(snapshot.meshes, swapped.meshes));
  const auto after = plan_scene_wireframe_overlay_batches(swapped, 64, &before.batches);
  EXPECT_EQ(classify_instance_batch_update(before.batches, after.batches),
            RenderInstanceBatchUpdateKind::Rebuild);
  for (const auto &chunk : after.batches.chunks) {
    ASSERT_EQ(chunk.occurrences.size(), 1u);
    EXPECT_EQ(chunk.key.index_offset, chunk.occurrences.front() == 2
        ? snapshot.style.active_selection_wire_color : snapshot.style.selection_wire_color);
    EXPECT_EQ(chunk.key.geometry_payload, wires->content_id);
  }
  const auto repeated = plan_scene_wireframe_overlay_batches(swapped, 64, &after.batches);
  EXPECT_EQ(classify_instance_batch_update(after.batches, repeated.batches),
            RenderInstanceBatchUpdateKind::Unchanged);
}

TEST(SelectedMeshOutlineCacheTests, PersistentInstanceActiveBatchesShareTheOriginalMeshGeometry) {
  const auto payload = outline_quad();
  const auto wires = outline_quad_wires();
  RenderOverlaySelectedObjectSnapshot snapshot;
  for (const EntityId id : {EntityId{1}, EntityId{2}}) {
    snapshot.instance_selection_fills.push_back({.occurrence = id, .member = 3,
        .geometry_payload = payload, .geometry_content_id = {1},
        .vertices = payload->vertices, .indices = payload->indices});
    snapshot.instance_selection_wires.push_back({.occurrence = id, .member = 3,
        .geometry_payload = payload, .geometry_content_id = {1}, .wire_geometry_payload = wires,
        .vertices = payload->vertices, .indices = payload->indices, .wire_segments = wires->segments});
  }
  const auto ordinary = plan_instance_selection_overlay_batches(snapshot, 64);
  ASSERT_EQ(ordinary.fills.chunks.size(), 1u);
  ASSERT_EQ(ordinary.wires.chunks.size(), 1u);
  snapshot.instance_selection_fills[1].active_selected = true;
  snapshot.instance_selection_wires[1].active_selected = true;
  snapshot.instance_selection_wires[1].wire_color = snapshot.style.active_selection_wire_color;
  const auto active = plan_instance_selection_overlay_batches(snapshot, 64);
  ASSERT_EQ(active.fills.chunks.size(), 2u);
  ASSERT_EQ(active.wires.chunks.size(), 2u);
  EXPECT_EQ(classify_instance_batch_update(ordinary.fills, active.fills),
            RenderInstanceBatchUpdateKind::Rebuild);
  EXPECT_EQ(classify_instance_batch_update(ordinary.wires, active.wires),
            RenderInstanceBatchUpdateKind::Rebuild);
  for (const auto &chunk : active.fills.chunks) {
    EXPECT_EQ(chunk.key.geometry_payload, ordinary.fills.chunks.front().key.geometry_payload);
    EXPECT_EQ(chunk.occurrences.size(), 1u);
  }
  for (const auto &chunk : active.wires.chunks) {
    EXPECT_EQ(chunk.key.geometry_payload, wires->content_id);
    EXPECT_EQ(chunk.occurrences.size(), 1u);
  }
  const auto repeated = plan_instance_selection_overlay_batches(snapshot, 64);
  EXPECT_EQ(classify_instance_batch_update(active.fills, repeated.fills),
            RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(classify_instance_batch_update(active.wires, repeated.wires),
            RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(active.work.local_bounds_vertex_visit_count, payload->vertices.size());
}

TEST(SelectedMeshOutlineCacheTests, StackedAndIntersectingMeshesOnlyOutlineCombinedProjectedCoverage) {
  const std::array<std::vector<OutlineBox>, 3> assemblies{{
      // A pillar touches a wider base and roof: their contact contours are interior.
      {{{-3, 0, -2}, {3, 1, 2}}, {{-0.6f, 1, -0.6f}, {0.6f, 4, 0.6f}},
       {{-3, 4, -2}, {3, 5, 2}}},
      // The pillar intersects both slabs and emerges through the roof.
      {{{-3, 0, -2}, {3, 1, 2}}, {{-0.6f, 0.4f, -0.6f}, {0.6f, 5.6f, 0.6f}},
       {{-3, 4, -2}, {3, 5, 2}}},
      // Side overlap plus a completely contained shell.
      {{{-2, 0, -1}, {1, 3, 1}}, {{0, 1, -1}, {3, 4, 1}},
       {{-0.5f, 1.2f, -0.5f}, {0.5f, 2, 0.5f}}},
  }};
  for (std::size_t assembly = 0; assembly < assemblies.size(); ++assembly) {
    SCOPED_TRACE(assembly);
    for (const bool grouped : {false, true}) {
      SCOPED_TRACE(grouped ? "Selected group" : "One selected mesh payload");
      SceneSession session;
      ASSERT_NO_FATAL_FAILURE(seed_outline_boxes(session, assemblies[assembly], grouped));
      auto selected = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
      auto hierarchy = build_render_overlay_selection_snapshot(session.scene(), session.assets());
      EXPECT_EQ(selected.meshes.size(), grouped ? 0u : 1u);
      EXPECT_EQ(hierarchy.hierarchy_selection_wires.size(), grouped ? assemblies[assembly].size() : 0u);
      SelectedMeshOutlineCache cache;
      for (const auto projection : {ViewportOverlayProjection::Orthographic,
                                    ViewportOverlayProjection::Perspective}) {
        SCOPED_TRACE(static_cast<int>(projection));
        auto camera = perspective();
        camera.projection = projection;
        camera.eye = {8, 8, 12};
        camera.forward = normalize(QVec3{0, 2, 0} - camera.eye);
        camera.right = normalize(cross(camera.forward, QVec3{0, 1, 0}));
        camera.up = cross(camera.right, camera.forward);
        const auto &frame = cache.frame(&selected, camera, 0, &hierarchy);
        ASSERT_NO_FATAL_FAILURE(expect_box_union_silhouette(frame, camera, assemblies[assembly]));
        const auto work = cache.work_stats();
        const auto revision = cache.revision(0);
        for (int i = 0; i < 10; ++i) (void)cache.frame(&selected, camera, 0, &hierarchy);
        EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
        EXPECT_EQ(cache.work_stats().source_refreshes, work.source_refreshes);
        EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
        EXPECT_EQ(cache.revision(0), revision);
      }
    }
  }
}

TEST(SelectedMeshOutlineCacheTests, FullyCoveredProjectionHasNoContourEvenForSeparateFrontMesh) {
  const std::array<OutlineBox, 2> boxes{{
      {{-3, 0, -2}, {3, 4, 2}}, {{-0.5f, 1.5f, 4}, {0.5f, 2.5f, 5}},
  }};
  for (const bool grouped : {false, true}) {
    SCOPED_TRACE(grouped);
    SceneSession session;
    ASSERT_NO_FATAL_FAILURE(seed_outline_boxes(session, boxes, grouped));
    auto selected = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
    auto hierarchy = build_render_overlay_selection_snapshot(session.scene(), session.assets());
    SelectedMeshOutlineCache cache;
    for (const auto projection : {ViewportOverlayProjection::Orthographic,
                                  ViewportOverlayProjection::Perspective}) {
      SCOPED_TRACE(static_cast<int>(projection));
      ViewportOverlayCamera camera;
      camera.projection = projection;
      camera.eye = {0, 2, 20};
      const auto &frame = cache.frame(&selected, camera, 0, &hierarchy);
      ASSERT_NO_FATAL_FAILURE(expect_box_union_silhouette(frame, camera, boxes));
      for (const auto &line : frame.lines) {
        EXPECT_LE(line.start.z, 2);
        EXPECT_LE(line.end.z, 2);
      }
    }
  }
}

TEST(SelectedMeshOutlineCacheTests, DenseCoverageRetainsPartialBoundariesAndHoles) {
  std::vector<OutlineBox> boxes{
      {{-3, -3, -0.5f}, {-1, 3, 0.5f}},
      {{1, -3, -0.5f}, {3, 3, 0.5f}},
      {{-3, -3, -0.5f}, {3, -1, 0.5f}},
      {{-3, 1, -0.5f}, {3, 3, 0.5f}},
      // Coverage on either end of this foreground strip must leave the gaps visible.
      {{-5, -0.1f, 1}, {5, 0.1f, 1.2f}},
  };
  for (int index = 0; index < 96; ++index) {
    const float y = -2.8f + static_cast<float>(index % 24) * 0.2f;
    const float z = -0.4f + static_cast<float>(index / 24) * 0.1f;
    boxes.push_back({{-2.8f, y, z}, {-1.2f, y + 0.4f, z + 0.1f}});
  }
  SceneSession session;
  ASSERT_NO_FATAL_FAILURE(seed_outline_boxes(session, boxes, false));
  auto selected = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
  SelectedMeshOutlineCache cache;
  auto camera = front_orthographic();
  for (const auto projection : {ViewportOverlayProjection::Orthographic,
                                ViewportOverlayProjection::Perspective}) {
    camera.projection = projection;
    camera.eye = {0, 0, 20};
    ASSERT_NO_FATAL_FAILURE(expect_box_union_silhouette(
        cache.frame(&selected, camera, 0), camera, boxes));
    const auto work = cache.work_stats();
    for (int repeat = 0; repeat < 30; ++repeat) (void)cache.frame(&selected, camera, 0);
    EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
    EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
  }
}

TEST(SelectedMeshOutlineCacheTests, PerspectiveForwardAndNearPlaneInvalidateCoverageWithoutRebuildingTopology) {
  auto payload = std::make_shared<MeshGeometryPayload>();
  payload->vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{1, 1, 0}}, {{-1, 1, 0}}};
  payload->indices = {0, 1, 2, 0, 2, 3};
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.meshes.push_back({.geometry_payload = payload, .selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  SelectedMeshOutlineCache cache;
  ViewportOverlayCamera camera;
  camera.eye = {0, 0, 2};
  ASSERT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  const auto work = cache.work_stats();
  camera.near_plane = 3;
  EXPECT_TRUE(cache.frame(&snapshot, camera, 0).lines.empty());
  EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications + 1);
  camera.near_plane = 0.05f;
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  camera.forward = {0, 0, 1};
  EXPECT_TRUE(cache.frame(&snapshot, camera, 0).lines.empty());
  EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications + 3);
  camera.forward = {0, 0, -1};
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 4u);
  EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
  EXPECT_EQ(cache.work_stats().source_refreshes, work.source_refreshes);
}

TEST(SelectedMeshOutlineCacheTests, TopViewTorusKeepsHoleAndOuterSilhouetteWithoutTubeContours) {
  SceneSession session;
  ASSERT_NO_FATAL_FAILURE(create_shape(session, PrimitiveCreationKind::Torus));
  auto snapshot = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  const auto &mesh = snapshot.meshes.front();
  float inner = std::numeric_limits<float>::infinity(), outer = 0;
  for (const auto &vertex : mesh.vertices) {
    const auto point = transform_point(mesh.world_transform, vertex.position);
    const float radius = std::hypot(point.x, point.z);
    inner = std::min(inner, radius);
    outer = std::max(outer, radius);
  }
  ASSERT_GT(inner, 0);
  ASSERT_GT(outer, inner);
  ViewportOverlayCamera camera;
  camera.projection = ViewportOverlayProjection::Orthographic;
  camera.eye = {0, 10, 0};
  camera.forward = {0, -1, 0};
  camera.right = {1, 0, 0};
  camera.up = {0, 0, -1};
  SelectedMeshOutlineCache cache;
  const auto &frame = cache.frame(&snapshot, camera, 0);
  ASSERT_FALSE(frame.lines.empty());
  std::array<float, 2> swept_angles{};
  for (const auto &line : frame.lines) {
    const float start_radius = std::hypot(line.start.x, line.start.z);
    const float end_radius = std::hypot(line.end.x, line.end.z);
    const bool hole = std::abs(start_radius - inner) < std::abs(start_radius - outer);
    EXPECT_NEAR(start_radius, hole ? inner : outer, 1e-4f);
    EXPECT_NEAR(end_radius, hole ? inner : outer, 1e-4f);
    const float cross_value = line.start.x * line.end.z - line.start.z * line.end.x;
    const float dot_value = line.start.x * line.end.x + line.start.z * line.end.z;
    swept_angles[hole ? 0 : 1] += std::abs(std::atan2(cross_value, dot_value));
  }
  // Both rings must be complete and appear once, regardless of segment splitting.
  EXPECT_NEAR(swept_angles[0], 2 * std::acos(-1.0f), 1e-3f);
  EXPECT_NEAR(swept_angles[1], 2 * std::acos(-1.0f), 1e-3f);
}

/// Supplies the real modeling selection stream while counting source rebuilds.
class OutlineSelectionProvider final : public ViewportOverlaySelectionFrameProvider {
public:
  explicit OutlineSelectionProvider(SceneSession &session) : session_(session) {}
  ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++builds;
    return session_.modeling().selection_overlay_frame(options);
  }
  mutable int builds = 0;
  ModelingSelectionOverlayOptions options;
private:
  SceneSession &session_;
};

/// Captures the two renderer submissions without creating a native window.
class OutlineRecordingTarget final : public ViewportOverlaySubmissionTarget {
public:
  ViewportOverlayCameraUpdate update_overlay_cameras(std::size_t, std::uint8_t,
      ViewportOverlayCamera) override { return {true, true}; }
  void submit_persistent_overlay(std::size_t, std::uint8_t, ViewportOverlayFrame frame,
      ViewportOverlayCamera) override { persistent = std::move(frame); }
  void submit_dynamic_overlay(std::size_t, std::uint8_t, ViewportOverlayFrame frame,
      ViewportOverlayCamera) override { dynamic = std::move(frame); }
  ViewportOverlayFrame persistent, dynamic;
};

TEST(SelectedMeshOutlineCacheTests, SubmissionUsesRasterWidthForOffscreenRibbonPadding) {
  const auto payload = outline_quad();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.meshes.push_back({.geometry_payload = payload,
      .world_transform = compose_transform({.location = {2.05f, 0, 0}, .scale = {0.01f, 0.5f, 1}}),
      .selected = true, .active_selected = true,
      .vertices = payload->vertices, .indices = payload->indices});
  ViewportOverlaySubmissionRequest request;
  request.camera = front_orthographic();
  request.camera.viewport_size = {400, 400};
  request.camera.orthographic_size = 4;
  request.overlays_visible = true;
  request.selection_dimensions_visible = false;
  request.selected_mesh_outline_snapshot = &snapshot;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  auto settings = ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(ViewportFloatSettingId::SelectionEdgeLineSize, 2));
  (void)settings.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 2.5f);
  ASSERT_FLOAT_EQ(settings.float_value(ViewportFloatSettingId::ActiveSelectionLineSize), 2.5f);
  OutlineRecordingTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  (void)coordinator.submit(request, settings, target);
  EXPECT_TRUE(target.dynamic.lines.empty());
  request.raster_scale = 4;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);
  ASSERT_EQ(target.dynamic.lines.size(), 4u);
  for (const auto &line : target.dynamic.lines) {
    EXPECT_FLOAT_EQ(line.width_pixels, 10);
    EXPECT_EQ(line.style_role, ViewportOverlayStyleRole::ActiveSelectionWire);
  }
  EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  request.raster_scale = 1;
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);
  EXPECT_TRUE(target.dynamic.lines.empty());
}

TEST(SelectedMeshOutlineCacheTests, SubmissionRetainsFillAndControlsWithNoInternalSelectionWire) {
  SceneSession session;
  create_shape(session, PrimitiveCreationKind::Box);
  const auto object = session.primitive_creation().live_primitive_creation();
  ASSERT_TRUE(object);
  auto snapshot = build_render_overlay_selected_object_snapshot(session.scene(), session.assets(), true);
  OutlineSelectionProvider provider(session);
  OutlineRecordingTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  ViewportOverlaySubmissionRequest request;
  request.camera = perspective();
  request.overlays_visible = true;
  // Isolate silhouette submission from the independent dimension-box stream.
  request.selection_dimensions_visible = false;
  request.selection_frame_provider = &provider;
  request.selected_mesh_outline_snapshot = &snapshot;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  auto settings = ViewportSettings::defaults();
  (void)coordinator.submit(request, settings, target);
  EXPECT_TRUE(target.persistent.lines.empty());
  EXPECT_FALSE(target.persistent.triangles.empty());
  EXPECT_FALSE(target.dynamic.lines.empty());
  for (const auto &line : target.dynamic.lines) {
    EXPECT_EQ(line.source, ViewportOverlaySource::MeshOutline);
  }
  const auto expect_silhouette_passes = [&]() {
    const auto plan = OverlayRenderableBuilder::build_layer(
        target.dynamic, request.camera, ViewportOverlayLayer::SelectedEdge);
    ASSERT_FALSE(plan.line_batches.empty());
    std::size_t vertex_count = 0u;
    for (const auto &batch : plan.line_batches) {
      vertex_count += batch.mesh.vertices.size();
      ASSERT_TRUE(batch.occluded_state.has_value());
      EXPECT_EQ(batch.state.depth_func, OverlayDepthFunc::GreaterOrEqual);
      EXPECT_EQ(batch.occluded_state->depth_func, OverlayDepthFunc::Less);
      EXPECT_EQ(batch.state.silhouette_pass, OverlaySilhouettePass::Visible);
      EXPECT_EQ(batch.occluded_state->silhouette_pass,
                OverlaySilhouettePass::Occluded);
      EXPECT_LT(batch.state.blend_order, batch.occluded_state->blend_order);
      EXPECT_FLOAT_EQ(batch.occluded_alpha_multiplier, 0.5f);
    }
    EXPECT_EQ(vertex_count, target.dynamic.lines.size() * 4u);
  };
  expect_silhouette_passes();
  const auto work = coordinator.work_counters();
  for (int i = 0; i < 30; ++i) EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  EXPECT_EQ(provider.builds, 1);
  request.camera.eye.x = -5;
  request.camera.fov_degrees = 130;
  const auto moved = coordinator.submit(request, settings, target);
  EXPECT_TRUE(moved.dynamic_submitted);
  EXPECT_FALSE(moved.persistent_submitted);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, work.dynamic_source_compose_count);
  EXPECT_EQ(provider.builds, 1);
  expect_silhouette_passes();
  request.raster_scale = 2;
  (void)coordinator.submit(request, settings, target);
  EXPECT_NEAR(target.dynamic.lines.front().width_pixels,
      settings.float_value(ViewportFloatSettingId::SelectionEdgeLineSize) * 2, 0.001f);
  request.construction_object_id = object->modeling_object_id;
  (void)coordinator.submit(request, settings, target);
  EXPECT_FALSE(target.persistent.lines.empty());
  EXPECT_TRUE(target.persistent.triangles.empty());
  EXPECT_TRUE(target.dynamic.lines.empty());
  request.construction_object_id = -1;
  request.scene_selection_mode = RenderOverlaySelectionMode::Component;
  (void)coordinator.submit(request, settings, target);
  EXPECT_FALSE(target.persistent.lines.empty());
  EXPECT_TRUE(target.dynamic.lines.empty());
}

TEST(SelectedMeshOutlineCacheTests, GroupMembersUseOutlinesAndKeepBoundsWithStableFrameGates) {
  SceneSession session;
  create_shape(session, PrimitiveCreationKind::Box);
  auto &document = session.scene();
  const auto mesh = document.selected_entities().front();
  const auto binding = *document.mesh(mesh);
  const auto original_transform = *document.transform(mesh);
  const auto group = document.create_entity("Three subdivided boxes", EntityKind::Empty);
  ASSERT_TRUE(document.set_parent(mesh, group, false));
  for (int i = 1; i < 3; ++i) {
    const auto child = document.create_entity("Box", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(child, binding));
    ASSERT_TRUE(document.set_parent(child, group, false));
    auto child_transform = original_transform;
    child_transform.location.y += 3.0f * i;
    ASSERT_TRUE(document.set_transform(child, child_transform));
  }
  document.clear_selection();
  ASSERT_TRUE(document.set_selected(group, true));
  auto selected = build_render_overlay_selected_object_snapshot(document, session.assets(), true);
  auto hierarchy = build_render_overlay_selection_snapshot(document, session.assets());
  ASSERT_TRUE(selected.meshes.empty());
  ASSERT_EQ(hierarchy.hierarchy_selection_wires.size(), 3u);
  ASSERT_EQ(hierarchy.group_bounds.size(), 1u);

  SelectedMeshOutlineCache cache;
  auto camera = perspective();
  const auto &outlines = cache.frame(&selected, camera, 0, &hierarchy);
  ASSERT_FALSE(outlines.lines.empty());
  const std::array<OutlineBox, 3> boxes{{
      {{-1, 0, -1}, {1, 2, 1}}, {{-1, 3, -1}, {1, 5, 1}},
      {{-1, 6, -1}, {1, 8, 1}},
  }};
  ASSERT_NO_FATAL_FAILURE(expect_box_union_silhouette(outlines, camera, boxes));
  const auto work = cache.work_stats();
  EXPECT_EQ(work.topology_builds, 1u);
  for (int i = 0; i < 30; ++i) (void)cache.frame(&selected, camera, 0, &hierarchy);
  EXPECT_EQ(cache.work_stats().source_refreshes, work.source_refreshes);
  EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
  camera.eye.x = -5;
  camera.fov_degrees = 130;
  camera.forward = normalize(QVec3{0, 3.5f, 0} - camera.eye);
  camera.right = normalize(cross(camera.forward, QVec3{0, 1, 0}));
  camera.up = cross(camera.right, camera.forward);
  (void)cache.frame(&selected, camera, 0, &hierarchy);
  EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
  EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications + 1);

  OutlineRecordingTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  ViewportOverlaySubmissionRequest request;
  request.camera = camera;
  request.overlays_visible = request.scene_overlay_requested = true;
  // Dimension boxes intentionally replace ordinary object-mode group cages;
  // this fixture verifies the underlying group cage and silhouette streams.
  request.selection_dimensions_visible = false;
  request.selected_mesh_outline_snapshot = &selected;
  request.scene_overlay_snapshot = &hierarchy;
  request.scene_selection_mode = RenderOverlaySelectionMode::Group;
  const auto settings = ViewportSettings::defaults();
  for (const auto mode : {RenderOverlaySelectionMode::Group, RenderOverlaySelectionMode::Object}) {
    request.scene_selection_mode = mode;
    (void)coordinator.submit(request, settings, target);
    ASSERT_FALSE(target.persistent.lines.empty());
    for (const auto &line : target.persistent.lines) {
      EXPECT_EQ(line.source, ViewportOverlaySource::GroupBounds);
    }
    ASSERT_FALSE(target.dynamic.lines.empty());
    for (const auto &line : target.dynamic.lines) {
      EXPECT_EQ(line.source, ViewportOverlaySource::MeshOutline);
    }
    EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  }
  ASSERT_TRUE(document.set_transform(group, TransformComponent{.location = {10, 0, 0}}));
  hierarchy = build_render_overlay_selection_snapshot(document, session.assets());
  const auto &moved = cache.frame(&selected, camera, 0, &hierarchy);
  ASSERT_FALSE(moved.lines.empty());
  for (const auto &line : moved.lines) EXPECT_GE(line.start.x, 9);
  EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
  EXPECT_TRUE(coordinator.submit(request, settings, target).dynamic_submitted);

  document.clear_selection();
  hierarchy = build_render_overlay_selection_snapshot(document, session.assets());
  EXPECT_TRUE(cache.frame(&selected, camera, 0, &hierarchy).lines.empty());
  (void)coordinator.submit(request, settings, target);
  EXPECT_TRUE(target.persistent.lines.empty());
  EXPECT_TRUE(target.dynamic.lines.empty());
}

constexpr std::size_t kLargeSelectionCameraSteps = 8;

/// Keeps bounded camera workloads derived from the loaded scene and its framing owner.
struct LargeSelectionCameraRegime {
  std::string_view name;
  std::array<ViewportOverlayCamera, kLargeSelectionCameraSteps> cameras;
};

std::array<LargeSelectionCameraRegime, 4> large_selection_camera_regimes(
    ViewportCameraController saved, const ViewportFocusBounds &bounds, QVec2 viewport_size) {
  saved.set_viewport_size(viewport_size.x, viewport_size.y);
  auto framed = saved;
  // Matches QtViewportPanel::frame_selection, including its deliberate framing margin.
  framed.focus_bounds(bounds.min, bounds.max, 1.4f);
  const auto framed_pose = framed.pose();
  const auto center = (bounds.min + bounds.max) * 0.5f;
  const auto extent = bounds.max - bounds.min;
  const float radius = std::max(length(extent) * 0.5f, saved.state().near_plane * 4);
  std::array<LargeSelectionCameraRegime, 4> regimes{{
      {.name = "saved_orbit"}, {.name = "framed_orbit"},
      {.name = "interior"}, {.name = "orthographic"}}};
  for (std::size_t step = 0; step < kLargeSelectionCameraSteps; ++step) {
    auto saved_step = saved;
    if (step != 0) {
      saved_step.begin_drag({}, ViewportDragMode::Orbit, saved.state().target);
      saved_step.update_drag_delta({static_cast<float>(step) * 8, static_cast<float>(step) * 2});
      saved_step.end_drag();
    }
    regimes[0].cameras[step] = ViewportCameraAdapter::overlay_camera(saved_step, viewport_size);

    const float angle = static_cast<float>(step) * 2 * std::acos(-1.0f) /
        static_cast<float>(kLargeSelectionCameraSteps);
    const float cosine = std::cos(angle), sine = std::sin(angle);
    const auto rotate_y = [cosine, sine](QVec3 point) {
      return QVec3{point.x * cosine + point.z * sine, point.y,
                   -point.x * sine + point.z * cosine};
    };
    auto orbit = framed;
    if (step != 0) {
      EXPECT_TRUE(orbit.restore_perspective_pose({
          .eye = framed_pose.target + rotate_y(framed_pose.eye - framed_pose.target),
          .target = framed_pose.target, .up = rotate_y(framed_pose.up)}));
    }
    regimes[1].cameras[step] = ViewportCameraAdapter::overlay_camera(orbit, viewport_size);

    auto interior = framed;
    const QVec3 eye = center + QVec3{extent.x * 0.15f * cosine,
        extent.y * 0.075f * std::sin(2 * angle), extent.z * 0.15f * sine};
    const QVec3 direction = normalize(QVec3{cosine, -0.08f, sine});
    EXPECT_TRUE(interior.restore_perspective_pose({
        .eye = eye, .target = eye + direction * (radius * 0.5f), .up = {0, 1, 0}}));
    regimes[2].cameras[step] = ViewportCameraAdapter::overlay_camera(interior, viewport_size);

    auto orthographic = framed;
    orthographic.view_axis(step < 4 ? ViewportViewAxis::Y : ViewportViewAxis::Z, 1);
    orthographic.focus_bounds(bounds.min, bounds.max, 1.4f);
    if (step % 4 != 0) {
      orthographic.begin_drag({}, ViewportDragMode::Pan);
      orthographic.update_drag_delta({static_cast<float>(step % 4) * 12, 0});
      orthographic.end_drag();
    }
    regimes[3].cameras[step] = ViewportCameraAdapter::overlay_camera(orthographic, viewport_size);
  }
  return regimes;
}

TEST(SelectedMeshOutlineCacheTests, LargeSceneSelectAllRetainsSubmittedOverlays) {
  const char *source = std::getenv("QUADER_TEST_LARGE_SELECTION_PROJECT");
  if (source == nullptr || *source == '\0') {
    GTEST_SKIP() << "Set QUADER_TEST_LARGE_SELECTION_PROJECT to measure a cloned project.";
  }
  const std::filesystem::path source_project = std::filesystem::u8path(source);
  ASSERT_TRUE(std::filesystem::is_regular_file(source_project));
  const char *source_scene = std::getenv("QUADER_TEST_LARGE_SELECTION_SCENE");
  const std::filesystem::path original_scene = source_scene != nullptr && *source_scene != '\0'
      ? std::filesystem::u8path(source_scene)
      : source_project.parent_path() / "Scenes" / "one.qdr";
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path root(temporary.path().toStdWString());
  const auto project_root = root / source_project.parent_path().filename();
  std::error_code error;
  std::filesystem::copy(source_project.parent_path(), project_root,
      std::filesystem::copy_options::recursive |
          std::filesystem::copy_options::skip_symlinks, error);
  ASSERT_FALSE(error) << error.message();
  const auto relative_scene = std::filesystem::relative(
      original_scene, source_project.parent_path(), error);
  ASSERT_FALSE(error) << error.message();
  ProjectSession project(root / "recent-scenes.json");
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
      "resources/modeling_assets");
  QuaderEditorApi editor(project);
  const auto opened_project = editor.project().open(project_root / source_project.filename());
  ASSERT_TRUE(opened_project.ok()) << opened_project.error_message();
  const auto opened = editor.scene_documents().open_native_path(project_root / relative_scene);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  auto &session = *project.active_scene()->session;
  const auto mark = [](const char *phase, std::chrono::steady_clock::time_point start) {
    const double milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "LARGE_SELECTION " << phase << "_ms=" << milliseconds << std::endl;
  };
  auto start = std::chrono::steady_clock::now();
  const auto selected = editor.selection().select_all();
  mark("select_all", start);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  ASSERT_GE(session.scene().selected_entities().size(), 6'000u);
  start = std::chrono::steady_clock::now();
  RenderOverlaySnapshotCache snapshots;
  (void)snapshots.update_persistent(session.scene(), session.assets(), true, 0, {},
      {}, nullptr, false, {}, {}, true);
  const auto &hierarchy = snapshots.selection_snapshot(session.scene(), session.assets());
  mark("snapshot", start);
  const auto *objects = snapshots.selected_object_snapshot();
  ASSERT_NE(objects, nullptr);
  std::size_t triangles = 0;
  for (const auto &mesh : objects->meshes) triangles += mesh.indices.size() / 3;
  std::cout << "LARGE_SELECTION meshes=" << objects->meshes.size()
            << " triangles=" << triangles << std::endl;
  OutlineSelectionProvider provider(session);
  provider.options.suppress_persistent_object_selection = true;
  OutlineRecordingTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  ViewportOverlaySubmissionRequest request;
  request.camera = ViewportCameraAdapter::overlay_camera(
      session.viewport_workspace().camera(ViewportPaneId::Perspective), {1280, 720});
  request.overlays_visible = request.scene_overlay_requested = true;
  request.selection_dimensions_visible = false;
  request.selection_frame_provider = &provider;
  request.selected_mesh_outline_snapshot = objects;
  request.scene_overlay_snapshot = &hierarchy;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  const auto settings = ViewportSettings::defaults();
  start = std::chrono::steady_clock::now();
  const auto first = coordinator.submit(request, settings, target);
  mark("first_submission", start);
  EXPECT_TRUE(first.dynamic_submitted);
  EXPECT_FALSE(target.dynamic.lines.empty());
  const auto work = coordinator.work_counters();
  start = std::chrono::steady_clock::now();
  for (int i = 0; i < 30; ++i) EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  mark("unchanged_30", start);
  EXPECT_EQ(provider.builds, 1);
  request.camera.eye.x += 1;
  start = std::chrono::steady_clock::now();
  (void)coordinator.submit(request, settings, target);
  mark("camera_move", start);
  EXPECT_EQ(provider.builds, 1);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
      work.dynamic_source_compose_count);

  const auto bounds = ViewportSceneProjection::selection_bounds(session.modeling());
  ASSERT_TRUE(bounds.has_value());
  const auto camera_regimes = large_selection_camera_regimes(
      session.viewport_workspace().camera(ViewportPaneId::Perspective), *bounds, {1280, 720});
  std::cout << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "LARGE_SELECTION bounds_min=" << bounds->min.x << ',' << bounds->min.y << ',' << bounds->min.z
            << " bounds_max=" << bounds->max.x << ',' << bounds->max.y << ',' << bounds->max.z
            << " path=cpu_fallback steps_per_regime=" << kLargeSelectionCameraSteps << std::endl;
  for (const auto &regime : camera_regimes) {
    std::array<double, kLargeSelectionCameraSteps> elapsed{};
    double unchanged_milliseconds = 0;
    for (std::size_t step = 0; step < regime.cameras.size(); ++step) {
      request.camera = regime.cameras[step];
      start = std::chrono::steady_clock::now();
      (void)coordinator.submit(request, settings, target);
      elapsed[step] = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start).count();
      const auto &camera = request.camera;
      std::cout << "LARGE_SELECTION_CAMERA regime=" << regime.name << " step=" << step
                << " submit_ms=" << elapsed[step] << " lines=" << target.dynamic.lines.size()
                << " eye=" << camera.eye.x << ',' << camera.eye.y << ',' << camera.eye.z
                << " forward=" << camera.forward.x << ',' << camera.forward.y << ',' << camera.forward.z
                << " right=" << camera.right.x << ',' << camera.right.y << ',' << camera.right.z
                << " up=" << camera.up.x << ',' << camera.up.y << ',' << camera.up.z
                << " viewport=" << camera.viewport_size.x << ',' << camera.viewport_size.y
                << " projection=" << static_cast<int>(camera.projection)
                << " fov=" << camera.fov_degrees << " ortho_size=" << camera.orthographic_size
                << " near=" << camera.near_plane << " far=" << camera.far_plane << std::endl;
      start = std::chrono::steady_clock::now();
      for (int repeat = 0; repeat < 5; ++repeat) {
        EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
      }
      unchanged_milliseconds += std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start).count();
      EXPECT_EQ(provider.builds, 1);
      EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count,
          work.dynamic_source_compose_count);
    }
    const double first_milliseconds = elapsed.front();
    const double mean_milliseconds = std::accumulate(elapsed.begin(), elapsed.end(), 0.0) / elapsed.size();
    std::sort(elapsed.begin(), elapsed.end());
    const auto percentile_index = static_cast<std::size_t>(std::ceil(elapsed.size() * 0.95)) - 1;
    std::cout << "LARGE_SELECTION_REGIME name=" << regime.name << " samples=" << elapsed.size()
              << " first_ms=" << first_milliseconds << " mean_ms=" << mean_milliseconds
              << " median_ms=" << elapsed[elapsed.size() / 2] << " p95_ms=" << elapsed[percentile_index]
              << " max_ms=" << elapsed.back() << " unchanged_40_ms=" << unchanged_milliseconds
              << std::endl;
  }
}

} // namespace
} // namespace quader

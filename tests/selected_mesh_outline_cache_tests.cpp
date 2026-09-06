////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_mesh_outline_cache.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "assets/asset_registry.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
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

TEST(SelectedMeshOutlineCacheTests, SubdividedBoxHasOnlySilhouetteAndDepthTestedEdges) {
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
    EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
    EXPECT_EQ(cache.revision(0), revision);
    camera.fov_degrees = 40;
    camera.viewport_size = {1600, 900};
    (void)cache.frame(&snapshot, camera, 0);
    EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications);
    camera.eye.x = -5;
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
    camera.eye = {100, 200, 300};
    camera.orthographic_size = 100;
    camera.fov_degrees = 75;
    (void)cache.frame(&snapshot, camera, 1);
    EXPECT_EQ(cache.work_stats().camera_classifications, ortho_work.camera_classifications);
    EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
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
      {.position = {4, 0, 0}, .rotation = {0, 0.3826834f, 0, 0.9238795f}, .scale = {-2, 3, 1}});
  ++snapshot.selection_revision;
  const auto &frame = cache.frame(&snapshot, camera, 0);
  EXPECT_EQ(frame.lines.size(), 8u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  ++snapshot.transform_revision;
  snapshot.meshes.back().world_transform = compose_transform({.position = {8, 0, 0}});
  EXPECT_EQ(cache.frame(&snapshot, camera, 0).lines.size(), 8u);
  EXPECT_EQ(cache.work_stats().topology_builds, 1u);
  cache.reset();
  EXPECT_TRUE(cache.frame(nullptr, camera, 0).lines.empty());
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
    return session_.modeling().selection_overlay_frame();
  }
  mutable int builds = 0;
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
  request.selection_frame_provider = &provider;
  request.selected_mesh_outline_snapshot = &snapshot;
  request.selection_source.selection_context = ViewportOverlaySelectionContext::Object;
  request.style_selection_context = ViewportOverlaySelectionContext::Object;
  auto settings = ViewportSettings::defaults();
  (void)coordinator.submit(request, settings, target);
  EXPECT_TRUE(target.persistent.lines.empty());
  EXPECT_FALSE(target.persistent.triangles.empty());
  EXPECT_FALSE(target.dynamic.lines.empty());
  const auto work = coordinator.work_counters();
  for (int i = 0; i < 30; ++i) EXPECT_TRUE(coordinator.submit(request, settings, target).camera_only);
  EXPECT_EQ(provider.builds, 1);
  request.camera.eye.x = -5;
  const auto moved = coordinator.submit(request, settings, target);
  EXPECT_TRUE(moved.dynamic_submitted);
  EXPECT_FALSE(moved.persistent_submitted);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, work.dynamic_source_compose_count);
  EXPECT_EQ(provider.builds, 1);
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
    child_transform.position.y += 3.0f * i;
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
  (void)cache.frame(&selected, camera, 0, &hierarchy);
  EXPECT_EQ(cache.work_stats().topology_builds, work.topology_builds);
  EXPECT_EQ(cache.work_stats().camera_classifications, work.camera_classifications + 1);

  OutlineRecordingTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  ViewportOverlaySubmissionRequest request;
  request.camera = camera;
  request.overlays_visible = request.scene_overlay_requested = true;
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
  ASSERT_TRUE(document.set_transform(group, TransformComponent{.position = {10, 0, 0}}));
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

} // namespace
} // namespace quader

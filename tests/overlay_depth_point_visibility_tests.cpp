////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_material_library.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/render_layers.hpp"
#include "render/render_scene_snapshot.hpp"

#include <gtest/gtest.h>

#include <filament/Scene.h>

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::array kVertexLayers{
    ViewportOverlayLayer::UnselectedVertex,
    ViewportOverlayLayer::SelectedVertex,
    ViewportOverlayLayer::HoverVertex,
};

ViewportOverlayCamera point_visibility_camera() {
  return {
      .eye = {},
      .forward = {0.0f, 0.0f, -1.0f},
      .right = {1.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
      .viewport_size = {1280.0f, 720.0f},
      .projection = ViewportOverlayProjection::Orthographic,
      .orthographic_size = 8.0f,
      .near_plane = 0.1f,
      .far_plane = 2000.0f,
  };
}

void append_visibility_box(ViewportOverlayFrame &frame, int object_id,
                           QVec3 offset = {}, bool inside_out = false) {
  const std::array<QVec3, 8u> corners{{
      {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
      {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
      {-1.0f, -1.0f, -3.0f}, {1.0f, -1.0f, -3.0f},
      {1.0f, 1.0f, -3.0f}, {-1.0f, 1.0f, -3.0f},
  }};
  constexpr std::array<std::array<std::size_t, 3u>, 12u> kTriangles{{
      {0u, 1u, 2u}, {0u, 2u, 3u}, {4u, 7u, 6u}, {4u, 6u, 5u},
      {1u, 5u, 6u}, {1u, 6u, 2u}, {0u, 3u, 7u}, {0u, 7u, 4u},
      {3u, 2u, 6u}, {3u, 6u, 7u}, {0u, 4u, 5u}, {0u, 5u, 1u},
  }};
  for (const auto &indices : kTriangles) {
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::SourceWireDepthStamp,
        .a = corners[indices[0u]] + offset,
        .b = corners[indices[inside_out ? 2u : 1u]] + offset,
        .c = corners[indices[inside_out ? 1u : 2u]] + offset,
        .element = {.object_id = object_id},
    });
  }
  for (const auto layer : kVertexLayers) {
    for (int vertex_id = 0; vertex_id < 2; ++vertex_id) {
      frame.points.push_back({
          .layer = layer,
          .position = QVec3{0.25f, 0.25f, vertex_id == 0 ? -1.0f : -3.0f} + offset,
          .color = rgba8(255u, 211u, 31u),
          .size_pixels = 6.0f,
          .element = {.object_id = object_id, .vertex_id = vertex_id},
          .outline_growth_pixels = 1.0f,
      });
    }
  }
}

std::vector<std::pair<int, int>> point_bindings(
    const OverlayLayerBuildPlan &plan) {
  std::vector<std::pair<int, int>> result;
  for (const auto &batch : plan.point_batches) {
    for (const auto &binding : batch.mesh.position_stream_bindings) {
      result.emplace_back(binding.object_id, binding.vertex_id);
    }
  }
  return result;
}

/// Exercises the retained production proxy path without requiring a desktop.
class PointVisibilityCacheHarness final {
public:
  ~PointVisibilityCacheHarness() {
    if (engine_ == nullptr) return;
    (void)engine_->flushAndWait(10'000'000'000ULL);
    engine_->pumpMessageQueues();
    cache_.shutdown();
    factory_.shutdown();
    materials_.shutdown();
    if (scene_ != nullptr) engine_->destroy(scene_);
    filament::Engine::destroy(&engine_);
  }

  bool initialize() {
    const auto backend =
        qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") ==
                QStringLiteral("1")
            ? filament::Engine::Backend::VULKAN
            : filament::Engine::Backend::NOOP;
    engine_ = filament::Engine::create(backend);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    const QString resource_root = QCoreApplication::instance() != nullptr
        ? QCoreApplication::applicationDirPath()
        : qEnvironmentVariable("QT_PLUGIN_PATH");
    const std::filesystem::path resources(resource_root.toStdWString());
    if (scene_ == nullptr || !materials_.initialize(*engine_, resources)) {
      return false;
    }
    factory_.initialize(*engine_, *scene_, materials_.vertex_color_overlay(),
                        materials_.overlay_line(), materials_.edit_wire_line(),
                        materials_.overlay_point(), materials_.overlay_icon(),
                        resources);
    cache_.initialize(factory_);
    return true;
  }

  OverlayRenderableCache &cache() { return cache_; }

private:
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  FilamentMaterialLibrary materials_;
  OverlayProxyFactory factory_;
  OverlayRenderableCache cache_;
};

} // namespace

TEST(OverlayDepthPointVisibilityTests, ReusesOwnedFacesAcrossLayersAndCameras) {
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 7);
  OverlayDepthStampPointVisibilityFilter visibility;
  const auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
  auto camera = point_visibility_camera();
  for (int step = 0; step < 4; ++step) {
    if (step >= 2) {
      camera.eye = {0.0f, 0.0f, -4.0f};
      camera.forward = {0.0f, 0.0f, 1.0f};
    }
    camera.projection = step % 2 == 0
        ? ViewportOverlayProjection::Orthographic
        : ViewportOverlayProjection::Perspective;
    for (const auto layer : kVertexLayers) {
      const auto retained = OverlayRenderableBuilder::build_layer(
          frame, camera, layer, &signatures, 0u, &visibility);
      const auto transient = OverlayRenderableBuilder::build_layer(
          frame, camera, layer, &signatures);
      EXPECT_EQ(point_bindings(retained), point_bindings(transient));
      const std::vector<std::pair<int, int>> expected{
          {7, step >= 2 ? 1 : 0}, {7, step >= 2 ? 1 : 0}};
      EXPECT_EQ(point_bindings(retained), expected);
    }
    EXPECT_EQ(visibility.stats().build_count, 1u);
  }
  EXPECT_EQ(visibility.stats().object_count, 1u);
  EXPECT_EQ(visibility.stats().triangle_count, 12u);

  // Replacing the semantic frame may release its original storage even when
  // the source content signature is unchanged. Acceleration owns its faces.
  auto replacement = frame;
  frame = {};
  visibility.prepare(replacement, signatures, point_visibility_camera());
  EXPECT_FALSE(visibility.point_visible(replacement.points[1u]));
  EXPECT_EQ(visibility.stats().build_count, 1u);
}

TEST(OverlayDepthPointVisibilityTests,
     GeometryAndPreviewInvalidationPreserveOcclusionRules) {
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 7);
  OverlayDepthStampPointVisibilityFilter visibility;
  const auto prepare = [&](ViewportOverlayCamera camera) {
    visibility.prepare(frame, OverlayRenderableBuilder::frame_signatures(frame),
                        camera);
  };
  auto camera = point_visibility_camera();
  prepare(camera);
  EXPECT_FALSE(visibility.point_visible(frame.points[1u]));

  frame.points.front().color = rgba8(32u, 80u, 255u);
  prepare(camera);
  EXPECT_EQ(visibility.stats().build_count, 1u);
  camera.near_plane = 1.5f;
  prepare(camera);
  EXPECT_TRUE(visibility.point_visible(frame.points[1u]));
  EXPECT_EQ(visibility.stats().build_count, 1u);

  camera = point_visibility_camera();
  for (auto &triangle : frame.triangles) {
    triangle.a.x += 10.0f;
    triangle.b.x += 10.0f;
    triangle.c.x += 10.0f;
  }
  prepare(camera);
  EXPECT_TRUE(visibility.point_visible(frame.points[1u]));
  EXPECT_EQ(visibility.stats().build_count, 2u);

  frame = {};
  append_visibility_box(frame, 7, {}, true);
  prepare(camera);
  EXPECT_TRUE(visibility.point_visible(frame.points[1u]));
  EXPECT_EQ(visibility.stats().build_count, 3u);
  visibility.invalidate();
  prepare(camera);
  EXPECT_EQ(visibility.stats().build_count, 4u);

  frame.triangles.clear();
  prepare(camera);
  EXPECT_TRUE(visibility.point_visible(frame.points[1u]));
  EXPECT_EQ(visibility.stats().object_count, 0u);
  EXPECT_EQ(visibility.stats().triangle_count, 0u);
}

TEST(OverlayDepthPointVisibilityTests,
     SelectionParticipationRetainsRemainingObjectAcceleration) {
  ViewportOverlayFrame frame;
  for (int object_id = 7; object_id < 10; ++object_id) {
    append_visibility_box(frame, object_id);
  }
  OverlayDepthStampPointVisibilityFilter visibility;
  const auto camera = point_visibility_camera();
  const auto evaluate = [&] {
    return visibility.evaluate(frame, OverlayRenderableBuilder::frame_signatures(frame),
                               camera, false);
  };
  ASSERT_EQ(evaluate().size(), 18u);
  EXPECT_EQ(visibility.stats().object_build_count, 3u);

  // A replacement frame owns different storage and drops one participant.
  auto replacement = frame;
  std::erase_if(replacement.triangles, [](const auto &triangle) {
    return triangle.element.object_id == 8;
  });
  std::erase_if(replacement.points, [](const auto &point) {
    return point.element.object_id == 8;
  });
  frame = std::move(replacement);
  const auto subset = evaluate();
  ASSERT_EQ(subset.size(), 12u);
  EXPECT_EQ(visibility.stats().object_count, 2u);
  EXPECT_EQ(visibility.stats().triangle_count, 24u);
  EXPECT_EQ(visibility.stats().object_build_count, 3u);
  EXPECT_EQ(visibility.stats().object_reuse_count, 2u);
  for (std::size_t index = 0u; index < subset.size(); ++index) {
    EXPECT_EQ(subset[index], index % 2u == 0u ? 1u : 0u);
  }

  // A removed object has no owner lifetime in the frame. Returning under the
  // same ID builds its current geometry instead of reviving a historical BVH.
  append_visibility_box(frame, 8, {}, true);
  const auto returned = evaluate();
  ASSERT_EQ(returned.size(), 18u);
  EXPECT_EQ(visibility.stats().object_count, 3u);
  EXPECT_EQ(visibility.stats().object_build_count, 4u);
  EXPECT_EQ(visibility.stats().object_reuse_count, 4u);
  EXPECT_TRUE(std::ranges::all_of(returned.subspan(12u), [](std::uint8_t flag) {
    return flag != 0u;
  }));

  frame.triangles.clear();
  const auto removed = evaluate();
  EXPECT_EQ(visibility.stats().object_count, 0u);
  EXPECT_EQ(visibility.stats().triangle_count, 0u);
  EXPECT_EQ(visibility.stats().object_build_count, 4u);
  EXPECT_TRUE(std::ranges::all_of(removed, [](std::uint8_t flag) {
    return flag != 0u;
  }));
}

TEST(OverlayDepthPointVisibilityTests,
     AppearanceChangesReusePointQueriesWithEquivalentExtents) {
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 7);
  OverlayDepthStampPointVisibilityFilter visibility;
  const auto camera = point_visibility_camera();
  const auto evaluate = [&] {
    return visibility.evaluate(frame, OverlayRenderableBuilder::frame_signatures(frame),
                               camera);
  };
  const auto original = evaluate();
  const std::vector<std::uint8_t> expected(original.begin(), original.end());
  const auto initial = visibility.stats();
  for (auto &point : frame.points) {
    point.color = rgba8(32u, 80u, 255u);
    point.style_role = ViewportOverlayStyleRole::ActiveSelectedVertex;
    point.source = ViewportOverlaySource::ComponentSelection;
    point.layer = point.layer == ViewportOverlayLayer::SelectedVertex
        ? ViewportOverlayLayer::UnselectedVertex : ViewportOverlayLayer::SelectedVertex;
  }
  for (auto &triangle : frame.triangles) {
    triangle.color = rgba8(80u, 90u, 100u);
    triangle.style_role = ViewportOverlayStyleRole::SelectedSourceWire;
  }
  const auto recolored = evaluate();
  EXPECT_TRUE(std::ranges::equal(recolored, expected));
  EXPECT_EQ(visibility.stats().query_batch_count, initial.query_batch_count);
  EXPECT_EQ(visibility.stats().ray_query_count, initial.ray_query_count);
  EXPECT_EQ(visibility.stats().object_build_count, initial.object_build_count);

  frame.points[1u].size_pixels += 4.0f;
  (void)evaluate();
  EXPECT_EQ(visibility.stats().query_batch_count, initial.query_batch_count + 1u);
  frame.points[1u].outline_growth_pixels = 8.0f;
  (void)evaluate();
  EXPECT_EQ(visibility.stats().query_batch_count, initial.query_batch_count + 2u);
  frame.points[1u].position.z = -0.5f;
  EXPECT_EQ(evaluate()[1u], 1u);
  EXPECT_EQ(visibility.stats().query_batch_count, initial.query_batch_count + 3u);
  frame.points[1u].position.z = -3.0f;
  EXPECT_EQ(evaluate()[1u], 0u);
  frame.points[1u].layer = ViewportOverlayLayer::ToolHandle;
  EXPECT_EQ(evaluate()[1u], 1u);
  EXPECT_EQ(visibility.stats().object_build_count, initial.object_build_count);
}

TEST(OverlayDepthPointVisibilityTests,
     GeometryPreviewRebuildsOnlyChangedObjectsAndRefreshesBindings) {
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 7);
  append_visibility_box(frame, 8);
  OverlayDepthStampPointVisibilityFilter visibility;
  const auto camera = point_visibility_camera();
  const auto evaluate = [&] {
    return visibility.evaluate(frame, OverlayRenderableBuilder::frame_signatures(frame),
                               camera, false);
  };
  EXPECT_EQ(evaluate()[1u], 0u);
  EXPECT_EQ(visibility.stats().object_build_count, 2u);
  for (auto &triangle : frame.triangles) {
    if (triangle.element.object_id != 7) continue;
    triangle.a.x += 10.0f;
    triangle.b.x += 10.0f;
    triangle.c.x += 10.0f;
  }
  const auto moved = evaluate();
  EXPECT_EQ(moved[1u], 1u);
  EXPECT_EQ(moved[7u], 0u);
  EXPECT_EQ(visibility.stats().object_build_count, 3u);
  EXPECT_EQ(visibility.stats().object_reuse_count, 1u);
  frame.points[1u].element.object_id = 8;
  EXPECT_EQ(evaluate()[1u], 0u);
  EXPECT_EQ(visibility.stats().object_build_count, 3u);
  for (auto &triangle : frame.triangles) {
    if (triangle.element.object_id == 8) std::swap(triangle.b, triangle.c);
  }
  const auto inverted = evaluate();
  EXPECT_EQ(inverted[1u], 1u);
  EXPECT_EQ(inverted[7u], 1u);
  EXPECT_EQ(visibility.stats().object_build_count, 4u);
  EXPECT_EQ(visibility.stats().object_reuse_count, 2u);

  OverlayDepthStampPointVisibilityFilter fresh;
  EXPECT_TRUE(std::ranges::equal(inverted,
      fresh.evaluate(frame, OverlayRenderableBuilder::frame_signatures(frame),
                     camera, false)));
}

TEST(OverlayDepthPointVisibilityTests,
     RetainedProxySubmissionBuildsAccelerationOnceAcrossPointLayers) {
  PointVisibilityCacheHarness harness;
  ASSERT_TRUE(harness.initialize());
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 7);
  auto camera = point_visibility_camera();
  auto &cache = harness.cache();
  ASSERT_TRUE(cache.set_overlay_frame(frame, camera));
  // Selected/unselected passes share one proxy; hover keeps two pass proxies.
  EXPECT_EQ(cache.stats(0u).proxy_count, 3u);
  EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 1u);
  const auto retained_geometry = cache.stats(0u).point_geometry_update_count;
  for (int step = 0; step < 8; ++step) {
    camera.eye.x += 0.1f;
    EXPECT_TRUE(cache.update_overlay_camera(0u, kViewportOverlayLayerMask, camera));
    EXPECT_EQ(cache.stats(0u).proxy_count, 3u);
    EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 1u);
    EXPECT_EQ(cache.stats(0u).point_geometry_update_count, retained_geometry);
    EXPECT_EQ(cache.stats(0u).point_position_upload_count, 0u);
  }
  EXPECT_FALSE(cache.set_overlay_frame(frame, camera));
  std::vector<RenderToolPreviewMeshRecord> previews(1u);
  auto &preview = previews.front();
  preview.source_object_id = 7;
  preview.dynamic_positions = true;
  preview.layout_key = 1u;
  preview.position_revision = 1u;
  preview.mesh.edit_geometry().vertices = {
      {.position = frame.points[0u].position, .source_vertex_id = 0u},
      {.position = frame.points[1u].position, .source_vertex_id = 1u},
  };
  preview.dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(
      std::vector<QVec3>{frame.points[0u].position, frame.points[1u].position});
  const auto move_camera = [&] {
    camera.eye.x += 0.1f;
    EXPECT_TRUE(cache.update_overlay_camera(0u, kViewportOverlayLayerMask, camera));
  };
  ASSERT_TRUE(cache.set_transform_position_streams(previews));
  EXPECT_EQ(cache.stats(0u).point_visibility.triangle_count, 12u);
  move_camera();
  EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 1u);
  EXPECT_FALSE(cache.set_transform_position_streams(previews));
  move_camera();
  EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 1u);
  ++preview.position_revision;
  preview.dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(
      std::vector<QVec3>{QVec3{0.5f, 0.25f, -1.0f}, QVec3{0.5f, 0.25f, -3.0f}});
  ASSERT_TRUE(cache.set_transform_position_streams(previews));
  move_camera();
  EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 1u);
  ASSERT_TRUE(cache.set_transform_position_streams({}));
  move_camera();
  EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 1u);
  frame.points.front().color = rgba8(32u, 80u, 255u);
  EXPECT_TRUE(cache.set_overlay_frame(frame, camera));
  EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 1u);
  for (auto &triangle : frame.triangles) {
    triangle.a.x += 10.0f;
    triangle.b.x += 10.0f;
    triangle.c.x += 10.0f;
  }
  EXPECT_TRUE(cache.set_overlay_frame(frame, camera));
  EXPECT_EQ(cache.stats(0u).point_visibility.build_count, 2u);
  frame.triangles.clear();
  EXPECT_TRUE(cache.set_overlay_frame(frame, camera));
  EXPECT_EQ(cache.stats(0u).point_visibility.triangle_count, 0u);
  EXPECT_EQ(cache.stats(0u).proxy_count, 3u);
  cache.clear_slot(0u);
  EXPECT_EQ(cache.stats(0u).proxy_count, 0u);
}

TEST(OverlayDepthPointVisibilityTests, ParallelFlagsMatchScalarSourceObjectVisibility) {
  ViewportOverlayFrame frame;
  constexpr int kObjectCount = 1024;
  for (int object = 0; object < kObjectCount; ++object) {
    append_visibility_box(frame, object,
        {static_cast<float>(object % 32) * 3.0f,
         static_cast<float>(object / 32) * 3.0f, 0.0f}, object % 7 == 0);
  }
  const auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
  OverlayDepthStampPointVisibilityFilter serial;
  OverlayDepthStampPointVisibilityFilter parallel;
  OverlayDepthStampPointVisibilityFilter scalar;
  for (const auto projection : {ViewportOverlayProjection::Orthographic,
                                ViewportOverlayProjection::Perspective}) {
    for (const bool allow_skips : {false, true}) {
      auto camera = point_visibility_camera();
      camera.projection = projection;
      camera.eye = {30.0f, 30.0f, 80.0f};
      camera.forward = normalize(QVec3{45.0f, 45.0f, -2.0f} - camera.eye);
      for (const float near_plane : {0.1f, 82.0f}) {
        camera.near_plane = near_plane;
        SCOPED_TRACE(near_plane);
        const auto serial_flags = serial.evaluate(frame, signatures, camera, allow_skips, false);
        const auto parallel_flags = parallel.evaluate(frame, signatures, camera, allow_skips, true);
        ASSERT_EQ(serial_flags.size(), frame.points.size());
        ASSERT_EQ(parallel_flags.size(), frame.points.size());
        scalar.prepare(frame, signatures, camera, allow_skips);
        for (std::size_t index = 0u; index < frame.points.size(); ++index) {
          EXPECT_EQ(parallel_flags[index], serial_flags[index]);
          EXPECT_EQ(parallel_flags[index] != 0u, scalar.point_visible(frame.points[index]));
        }
        const auto queries = parallel.stats().query_batch_count;
        (void)parallel.evaluate(frame, signatures, camera, allow_skips);
        EXPECT_EQ(parallel.stats().query_batch_count, queries);
      }
    }
  }
  EXPECT_EQ(serial.stats().parallel_query_batch_count, 0u);
  if (std::thread::hardware_concurrency() > 1u) {
    EXPECT_GT(parallel.stats().parallel_query_batch_count, 0u);
  }
  EXPECT_EQ(parallel.stats().build_count, 1u);
  EXPECT_EQ(serial.stats().build_count, 1u);
}

TEST(OverlayDepthPointVisibilityTests, CameraAndOccluderChangesRetainPointGeometry) {
  PointVisibilityCacheHarness harness;
  ASSERT_TRUE(harness.initialize());
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 7);
  auto camera = point_visibility_camera();
  auto &cache = harness.cache();
  ASSERT_TRUE(cache.set_overlay_frame(frame, camera));
  const auto original = cache.stats(0u);
  EXPECT_EQ(original.point_vertex_count, 32u);
  EXPECT_EQ(original.visible_point_vertex_count, 16u);
  camera.eye = {0.0f, 0.0f, -4.0f};
  camera.forward = {0.0f, 0.0f, 1.0f};
  ASSERT_TRUE(cache.update_overlay_camera(0u, kViewportOverlayLayerMask, camera));
  const auto reversed = cache.stats(0u);
  EXPECT_EQ(reversed.point_geometry_update_count, original.point_geometry_update_count);
  EXPECT_EQ(reversed.point_position_upload_count, 0u);
  EXPECT_EQ(reversed.point_vertex_count, original.point_vertex_count);
  EXPECT_EQ(reversed.visible_point_vertex_count, 16u);
  EXPECT_GT(reversed.point_visibility_upload_count, original.point_visibility_upload_count);
  EXPECT_EQ(reversed.point_visibility.build_count, 1u);

  frame.triangles.clear();
  ASSERT_TRUE(cache.set_overlay_frame(frame, camera));
  const auto no_occluder = cache.stats(0u);
  EXPECT_EQ(no_occluder.visible_point_vertex_count, 32u);
  EXPECT_EQ(no_occluder.point_geometry_update_count, original.point_geometry_update_count);
  EXPECT_EQ(no_occluder.point_position_upload_count, 0u);
  EXPECT_GT(no_occluder.point_visibility_upload_count, reversed.point_visibility_upload_count);
  ASSERT_TRUE(cache.update_overlay_camera(0u, kViewportOverlayLayerMask, camera));
  EXPECT_EQ(cache.stats(0u).point_visibility_upload_count,
            no_occluder.point_visibility_upload_count);
}

TEST(OverlayDepthPointVisibilityTests, RetainedPointMappingsFollowInterleavedFrameOrder) {
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 7);
  std::rotate(frame.points.begin(), frame.points.begin() + 1, frame.points.end());
  frame.points.insert(frame.points.begin() + 2, {
      .layer = ViewportOverlayLayer::ToolHandle,
      .position = {0.0f, 0.0f, -1.0f},
  });
  const auto camera = point_visibility_camera();
  for (const auto layer : kVertexLayers) {
    const auto plan = OverlayRenderableBuilder::build_layer(
        frame, camera, layer, nullptr, 0u, nullptr, true, true);
    ASSERT_EQ(plan.point_batches.size(), 2u);
    for (const auto &batch : plan.point_batches) {
      ASSERT_EQ(batch.visibility_point_indices.size() * 4u, batch.mesh.vertices.size());
      for (std::size_t index = 0u; index < batch.visibility_point_indices.size(); ++index) {
        const auto frame_index = batch.visibility_point_indices[index];
        ASSERT_LT(frame_index, frame.points.size());
        EXPECT_EQ(frame.points[frame_index].layer, layer);
        const auto &center = batch.mesh.vertices[index * 4u].center;
        EXPECT_FLOAT_EQ(center.x, frame.points[frame_index].position.x);
        EXPECT_FLOAT_EQ(center.y, frame.points[frame_index].position.y);
        EXPECT_FLOAT_EQ(center.z, frame.points[frame_index].position.z);
      }
    }
  }
}

TEST(OverlayDepthPointVisibilityTests, OptInLargeComponentCameraBenchmark) {
  if (qEnvironmentVariable("QUADER_TEST_COMPONENT_POINT_VISIBILITY_BENCHMARK") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_TEST_COMPONENT_POINT_VISIBILITY_BENCHMARK=1";
  }
  constexpr int kObjectCount = 6274;
  constexpr int kCameraSteps = 8;
  ViewportOverlayFrame frame;
  frame.triangles.reserve(kObjectCount * 12u);
  frame.points.reserve(kObjectCount * 6u);
  for (int object_index = 0; object_index < kObjectCount; ++object_index) {
    append_visibility_box(frame, object_index,
        {static_cast<float>(object_index % 80) * 3.0f,
         static_cast<float>(object_index / 80) * 3.0f, 0.0f});
  }
  const auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
  const auto run = [&](bool retain) {
    OverlayDepthStampPointVisibilityFilter retained;
    std::uint64_t builds = 0u;
    std::uint64_t viewport_skips = 0u;
    std::uint64_t ray_queries = 0u;
    std::vector<std::size_t> binding_counts;
    const auto start = std::chrono::steady_clock::now();
    for (int step = 0; step < kCameraSteps; ++step) {
      auto camera = point_visibility_camera();
      camera.eye = {-10.0f + static_cast<float>(step) * 3.0f, 40.0f, 80.0f};
      camera.forward = normalize(QVec3{120.0f, 120.0f, -2.0f} - camera.eye);
      camera.projection = ViewportOverlayProjection::Perspective;
      for (const auto layer : kVertexLayers) {
        OverlayDepthStampPointVisibilityFilter transient;
        const auto plan = OverlayRenderableBuilder::build_layer(
            frame, camera, layer, &signatures, 0u,
            retain ? &retained : &transient);
        std::size_t count = 0u;
        for (const auto &batch : plan.point_batches) {
          count += batch.mesh.position_stream_bindings.size();
        }
        binding_counts.push_back(count);
        builds += transient.stats().build_count;
        viewport_skips += transient.stats().viewport_skip_count;
        ray_queries += transient.stats().ray_query_count;
      }
    }
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    builds += retained.stats().build_count;
    viewport_skips += retained.stats().viewport_skip_count;
    ray_queries += retained.stats().ray_query_count;
    std::cout << "[component-point-camera] retained=" << retain
              << " objects=" << kObjectCount
              << " triangles=" << frame.triangles.size()
              << " points=" << frame.points.size()
              << " cameras=" << kCameraSteps << " layers=3"
              << " acceleration_builds=" << builds
              << " viewport_skips=" << viewport_skips
              << " ray_queries=" << ray_queries
              << " elapsed_ms=" << elapsed_ms << '\n';
    EXPECT_EQ(builds, retain ? 1u : kCameraSteps * kVertexLayers.size());
    return binding_counts;
  };
  const auto transient = run(false);
  const auto retained = run(true);
  EXPECT_EQ(retained, transient);
}

TEST(OverlayDepthPointVisibilityTests,
     OffscreenPointsKeepTransformBindingsWithoutCastingVisibilityRays) {
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 11, {20.0f, 0.0f, 0.0f});
  const auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
  OverlayDepthStampPointVisibilityFilter visibility;
  auto camera = point_visibility_camera();
  for (const auto layer : kVertexLayers) {
    const auto plan = OverlayRenderableBuilder::build_layer(
        frame, camera, layer, &signatures, 0u, &visibility);
    const auto bindings = point_bindings(plan);
    EXPECT_NE(std::ranges::find(bindings, std::pair{11, 0}), bindings.end());
    EXPECT_NE(std::ranges::find(bindings, std::pair{11, 1}), bindings.end());
  }
  EXPECT_EQ(visibility.stats().build_count, 1u);
  EXPECT_EQ(visibility.stats().viewport_skip_count, 6u);
  EXPECT_EQ(visibility.stats().ray_query_count, 0u);

  camera.eye.x = 20.0f;
  for (const auto layer : kVertexLayers) {
    const auto plan = OverlayRenderableBuilder::build_layer(
        frame, camera, layer, &signatures, 0u, &visibility);
    const auto bindings = point_bindings(plan);
    EXPECT_NE(std::ranges::find(bindings, std::pair{11, 0}), bindings.end());
    EXPECT_EQ(std::ranges::find(bindings, std::pair{11, 1}), bindings.end());
  }
  EXPECT_EQ(visibility.stats().build_count, 1u);
  EXPECT_EQ(visibility.stats().viewport_skip_count, 6u);
  EXPECT_EQ(visibility.stats().ray_query_count, 6u);
}

TEST(OverlayDepthPointVisibilityTests,
     ViewportRayGateIncludesMarkerOutlineAtEveryBoundary) {
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 11);
  const auto signatures = OverlayRenderableBuilder::frame_signatures(frame);
  for (const auto projection : {ViewportOverlayProjection::Perspective,
                                ViewportOverlayProjection::Orthographic}) {
    SCOPED_TRACE(static_cast<int>(projection));
    auto camera = point_visibility_camera();
    camera.projection = projection;
    camera.viewport_size = {200.0f, 100.0f};
    camera.orthographic_size = 2.0f;
    camera.fov_degrees = 90.0f;
    const float half_height =
        projection == ViewportOverlayProjection::Perspective ? 3.0f : 1.0f;
    const float world_per_pixel = half_height / 50.0f;
    OverlayDepthStampPointVisibilityFilter visibility;
    visibility.prepare(frame, signatures, camera);
    for (const QVec2 boundary : {QVec2{-1.0f, 0.0f}, QVec2{1.0f, 0.0f},
                                 QVec2{0.0f, -1.0f}, QVec2{0.0f, 1.0f}}) {
      for (const float outside_pixels : {4.0f, 8.0f}) {
        const ViewportOverlayPoint point{
            .layer = ViewportOverlayLayer::SelectedVertex,
            .position = {
                boundary.x * (half_height * 2.0f +
                              outside_pixels * world_per_pixel),
                boundary.y * (half_height +
                              outside_pixels * world_per_pixel),
                -3.0f},
            .size_pixels = 6.0f,
            .element = {.object_id = 11, .vertex_id = 0},
            .outline_growth_pixels = 4.0f,
        };
        const auto before = visibility.stats();
        (void)visibility.point_visible(point);
        const auto after = visibility.stats();
        EXPECT_EQ(after.viewport_skip_count - before.viewport_skip_count,
                  outside_pixels == 8.0f ? 1u : 0u);
        EXPECT_EQ(after.ray_query_count - before.ray_query_count,
                  outside_pixels == 4.0f ? 1u : 0u);
      }
    }
    const auto before_near = visibility.stats();
    EXPECT_TRUE(visibility.point_visible({
        .layer = ViewportOverlayLayer::SelectedVertex,
        .position = {0.0f, 0.0f, -camera.near_plane * 0.5f},
        .size_pixels = 6.0f,
        .element = {.object_id = 11, .vertex_id = 0},
    }));
    EXPECT_EQ(visibility.stats().viewport_skip_count,
              before_near.viewport_skip_count + 1u);
    EXPECT_EQ(visibility.stats().ray_query_count, before_near.ray_query_count);
  }
}

TEST(OverlayDepthPointVisibilityTests,
     TransformStreamsRestoreStrictVisibilityBeforeMovingOffscreenPoints) {
  PointVisibilityCacheHarness harness;
  ASSERT_TRUE(harness.initialize());
  ViewportOverlayFrame frame;
  append_visibility_box(frame, 11, {20.0f, 0.0f, 0.0f});

  std::vector<RenderToolPreviewMeshRecord> previews(1u);
  auto &preview = previews.front();
  preview.source_object_id = 11;
  preview.dynamic_positions = true;
  preview.layout_key = 1u;
  preview.position_revision = 1u;
  auto &geometry = preview.mesh.edit_geometry();
  const auto source_vertex = [&](QVec3 position) {
    const auto found = std::ranges::find_if(
        geometry.vertices, [&](const MeshVertex &vertex) {
          return vertex.position.x == position.x &&
                 vertex.position.y == position.y &&
                 vertex.position.z == position.z;
        });
    if (found != geometry.vertices.end()) {
      return found->source_vertex_id;
    }
    const auto id = static_cast<std::uint32_t>(geometry.vertices.size());
    geometry.vertices.push_back({.position = position, .source_vertex_id = id});
    return id;
  };
  std::vector<ViewportOverlayTriangle> source_fills;
  for (auto &triangle : frame.triangles) {
    const std::uint32_t a = source_vertex(triangle.a);
    const std::uint32_t b = source_vertex(triangle.b);
    const std::uint32_t c = source_vertex(triangle.c);
    triangle.element.vertex_id = static_cast<int>(a);
    triangle.element.edge_a = static_cast<int>(b);
    triangle.element.edge_b = static_cast<int>(c);
    geometry.indices.insert(geometry.indices.end(), {a, b, c});
    auto fill = triangle;
    fill.layer = ViewportOverlayLayer::SelectedVisibleFace;
    source_fills.push_back(fill);
  }
  ASSERT_EQ(geometry.vertices.size(), 8u);
  for (auto &point : frame.points) {
    point.element.vertex_id = static_cast<int>(source_vertex(point.position));
  }
  ASSERT_EQ(geometry.vertices.size(), 10u);
  geometry.primitives.push_back({.index_count = 36u});
  frame.triangles.insert(frame.triangles.end(), source_fills.begin(),
                         source_fills.end());
  const auto moved_positions = [&](float offset) {
    auto positions = std::make_shared<std::vector<QVec3>>();
    positions->reserve(geometry.vertices.size());
    for (const auto &vertex : geometry.vertices) {
      positions->push_back(vertex.position + QVec3{offset, 0.0f, 0.0f});
    }
    return positions;
  };
  preview.dynamic_position_payload = moved_positions(-20.0f);

  auto camera = point_visibility_camera();
  auto &cache = harness.cache();
  ASSERT_TRUE(cache.set_overlay_frame(frame, camera));
  constexpr std::size_t kAllPointVertices = 32u;
  constexpr std::size_t kStrictPointVertices = 16u;
  ASSERT_EQ(cache.stats(0u).point_vertex_count, kAllPointVertices);
  ASSERT_EQ(cache.stats(0u).point_visibility.viewport_skip_count, 6u);
  ASSERT_EQ(cache.stats(0u).point_visibility.ray_query_count, 0u);
  const auto initial_geometry = cache.stats(0u).point_geometry_update_count;

  frame.lines.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .start = {-0.5f, 0.0f, -2.0f},
      .end = {0.5f, 0.0f, -2.0f},
  });
  ASSERT_TRUE(cache.set_overlay_frame(frame, camera));
  ASSERT_EQ(cache.stats(0u).point_vertex_count, kAllPointVertices);
  ASSERT_EQ(cache.stats(0u).point_visibility.viewport_skip_count, 6u)
      << "An unrelated frame change must retain the existing point proxies.";
  ASSERT_TRUE(cache.set_transform_position_streams(previews));
  EXPECT_EQ(cache.stats(0u).visible_point_vertex_count, kStrictPointVertices)
      << "The hidden base point must be masked in its retained GPU proxy before its "
         "position stream brings it onscreen.";
  EXPECT_EQ(cache.stats(0u).point_visibility.viewport_skip_count, 6u);
  EXPECT_EQ(cache.stats(0u).point_visibility.ray_query_count, 6u);
  EXPECT_EQ(cache.stats(0u).point_geometry_update_count, initial_geometry);
  EXPECT_EQ(cache.stats(0u).point_vertex_count, kAllPointVertices);
  EXPECT_FALSE(cache.set_transform_position_streams(previews));

  ++preview.position_revision;
  preview.dynamic_position_payload = moved_positions(-19.75f);
  ASSERT_TRUE(cache.set_transform_position_streams(previews));
  EXPECT_EQ(cache.stats(0u).visible_point_vertex_count, kStrictPointVertices);
  camera.eye.x += 0.1f;
  ASSERT_TRUE(cache.update_overlay_camera(0u, kViewportOverlayLayerMask, camera));
  EXPECT_EQ(cache.stats(0u).visible_point_vertex_count, kStrictPointVertices);
  EXPECT_EQ(cache.stats(0u).point_visibility.viewport_skip_count, 6u)
      << "Camera rebuilds during a packed transform must keep strict queries.";

  frame.points.front().color = rgba8(32u, 80u, 255u);
  ASSERT_TRUE(cache.set_overlay_frame(frame, camera));
  EXPECT_EQ(cache.stats(0u).visible_point_vertex_count, kStrictPointVertices);
  EXPECT_EQ(cache.stats(0u).point_visibility.viewport_skip_count, 6u)
      << "A replacement frame during the transform cannot re-enable skips.";

  ASSERT_TRUE(cache.set_transform_position_streams({}));
  camera.eye.x += 0.1f;
  ASSERT_TRUE(cache.update_overlay_camera(0u, kViewportOverlayLayerMask, camera));
  EXPECT_EQ(cache.stats(0u).point_vertex_count, kAllPointVertices);
  EXPECT_EQ(cache.stats(0u).visible_point_vertex_count, kAllPointVertices);
  EXPECT_EQ(cache.stats(0u).point_visibility.viewport_skip_count, 12u);
  ASSERT_TRUE(cache.set_transform_position_streams(previews));
  EXPECT_EQ(cache.stats(0u).visible_point_vertex_count, kStrictPointVertices)
      << "Starting a second gesture must restore strict visibility again.";
  cache.reset_transform_position_streams();
  camera.eye.x += 0.1f;
  ASSERT_TRUE(cache.update_overlay_camera(0u, kViewportOverlayLayerMask, camera));
  EXPECT_EQ(cache.stats(0u).point_vertex_count, kAllPointVertices);
}

} // namespace quader

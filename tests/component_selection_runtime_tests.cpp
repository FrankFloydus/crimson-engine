////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/component_selection_renderer.hpp"
#include "render/filament_material_library.hpp"
#include "render/png_texture_loader.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/session/scene_session.hpp"

#include <gtest/gtest.h>
#include <QString>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>

#include <array>
#include <cmath>
#include <filesystem>

namespace quader {
namespace {

/// Exercises production asynchronous transfers on an actual backend without Qt windows.
class ComponentSelectionRuntimeHarness final {
public:
  ~ComponentSelectionRuntimeHarness() {
    if (!engine_) return;
    (void)engine_->flushAndWait(5'000'000'000ull);
    engine_->pumpMessageQueues();
    selection.shutdown();
    materials_.shutdown();
    if (fallback_) engine_->destroy(fallback_);
    if (swapchain_) engine_->destroy(swapchain_);
    if (renderer_) engine_->destroy(renderer_);
    filament::Engine::destroy(&engine_);
  }

  bool initialize(filament::Engine::Backend backend) {
    engine_ = filament::Engine::create(backend);
    if (!engine_) return false;
    renderer_ = engine_->createRenderer();
    swapchain_ = engine_->createSwapChain(64u, 64u, filament::SwapChain::CONFIG_READABLE);
    const std::filesystem::path resources(qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
    if (!renderer_ || !swapchain_ || !materials_.initialize(*engine_, resources)) return false;
    const std::array<std::byte, 4u> pixel{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    fallback_ = load_rgba_texture(*engine_, pixel, 1u, 1u);
    return fallback_ && selection.initialize(*engine_, materials_.component_selection(), *fallback_);
  }

  std::shared_ptr<const ViewportSelectionDepthSnapshot> capture(
      const std::shared_ptr<const ViewportSelectionDepthGeometry> &geometry,
      const ViewportSelectionDepthCamera &camera) {
    selection.prepare(geometry, camera);
    for (unsigned int attempt = 0u; attempt < 12u; ++attempt) {
      if (renderer_->beginFrame(swapchain_)) {
        selection.render(*renderer_);
        renderer_->endFrame();
      }
      if (!engine_->flushAndWait(1'000'000'000ull)) return {};
      engine_->pumpMessageQueues();
      if (const auto snapshot = selection.source()->latest(); snapshot && snapshot->matches(geometry.get(), camera)) return snapshot;
    }
    return {};
  }

  ComponentSelectionRenderer selection;

private:
  filament::Engine *engine_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *fallback_ = nullptr;
  FilamentMaterialLibrary materials_;
};

std::shared_ptr<const ViewportSelectionDepthGeometry> upper_plane_geometry() {
  auto geometry = std::make_shared<ViewportSelectionDepthGeometry>();
  // The asymmetric footprint detects an upside-down readback or sample lookup.
  geometry->triangles.push_back({{-4, 2, -10}, {4, 2, -10}, {-4, 6, -10}});
  geometry->triangles.push_back({{4, 2, -10}, {4, 6, -10}, {-4, 6, -10}});
  return geometry;
}

ViewportSelectionDepthIntersection plane_intersections() {
  return {.intersect = [](const void *, std::uint32_t, QVec3 origin, QVec3 direction) -> std::optional<float> {
    if (direction.z == 0.0f) return {};
    const float distance = (-10.0f - origin.z) / direction.z;
    const QVec3 point = origin + direction * distance;
    return distance >= 0.0f && point.x >= -4.0f && point.x <= 4.0f && point.y >= 2.0f && point.y <= 6.0f
        ? std::optional<float>{distance} : std::nullopt;
  }};
}

TEST(ComponentSelectionRuntimeTests, NoopCompletionCannotCertifyUntouchedReadbackStorage) {
  ComponentSelectionRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::NOOP));
  ViewportSelectionDepthCamera camera;
  camera.viewport_size = {64, 64};
  camera.orthographic = true;
  camera.orthographic_size = 20;
  const auto snapshot = harness.capture(upper_plane_geometry(), camera);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(classify_viewport_selection_depth(*snapshot, {32, 20}, {0, 3.75f, 0}, {0, 0, -1}, 20, 0.001,
      plane_intersections()), ViewportSelectionDepthDecision::Unavailable);
}

TEST(ComponentSelectionRuntimeTests, VulkanReadbacksPreserveVisibleHiddenAndSubpixelRaySemantics) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_SELECTION_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_SELECTION_TESTS=1 for asynchronous Vulkan selection coverage";
  }
  ComponentSelectionRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN));
  ViewportSelectionDepthCamera camera;
  camera.viewport_size = {64, 64};
  camera.orthographic = true;
  camera.orthographic_size = 20;
  const auto geometry = upper_plane_geometry();
  EXPECT_FALSE(harness.selection.wants_geometry(camera, true));
  EXPECT_TRUE(harness.selection.wants_geometry(camera, true));
  const auto snapshot = harness.capture(geometry, camera);
  ASSERT_TRUE(snapshot);
  ASSERT_TRUE(snapshot->conservative_coverage);
  EXPECT_EQ(classify_viewport_selection_depth(*snapshot, {32, 20}, {0, 3.75f, 0}, {0, 0, -1}, 10, 0.001,
      plane_intersections()), ViewportSelectionDepthDecision::Accepted);
  EXPECT_EQ(classify_viewport_selection_depth(*snapshot, {32, 20}, {0, 3.75f, 0}, {0, 0, -1}, 20, 0.001,
      plane_intersections()), ViewportSelectionDepthDecision::Rejected);
  EXPECT_EQ(classify_viewport_selection_depth(*snapshot, {32, 44}, {0, -3.75f, 0}, {0, 0, -1}, 20, 0.001,
      plane_intersections()), ViewportSelectionDepthDecision::Accepted);
  // The same integer cell can contain a covered and an uncovered exact ray.
  EXPECT_EQ(classify_viewport_selection_depth(*snapshot, {44.79f, 20}, {3.996875f, 3.75f, 0}, {0, 0, -1}, 20, 0.001,
      plane_intersections()), ViewportSelectionDepthDecision::Rejected);
  EXPECT_EQ(classify_viewport_selection_depth(*snapshot, {44.99f, 20}, {4.059375f, 3.75f, 0}, {0, 0, -1}, 20, 0.001,
      plane_intersections()), ViewportSelectionDepthDecision::Accepted);
  const auto stats = harness.selection.stats();
  EXPECT_EQ(stats.submission_count, 1u);
  EXPECT_EQ(stats.completed_readbacks, 1u);
  EXPECT_EQ(stats.readback_bytes, 64u * 64u * 16u * 4u);
  EXPECT_EQ(harness.capture(geometry, camera), snapshot);
  EXPECT_EQ(harness.selection.stats().submission_count, 1u);
  camera.origin.x = 1;
  EXPECT_FALSE(snapshot->matches(geometry.get(), camera));
  EXPECT_FALSE(harness.selection.wants_geometry(camera, false));
}

TEST(ComponentSelectionRuntimeTests, DiscardedNearPlaneOccludersRequireTheExactCpuFallback) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_SELECTION_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_SELECTION_TESTS=1 for Vulkan near-plane coverage";
  }
  ComponentSelectionRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN));
  ViewportSelectionDepthCamera camera;
  camera.viewport_size = {64, 64};
  camera.orthographic = true;
  camera.orthographic_size = 20;
  constexpr std::array<float, 4u> kDepths{0.0000005f, 0.0f, -0.0000005f, -10.0f};
  for (const float depth : kDepths) {
    SCOPED_TRACE(depth);
    SceneSession session;
    MeshAsset mesh;
    auto &payload = mesh.edit_geometry();
    payload.vertices = {{.position = {-4, 2, -depth}}, {.position = {4, 2, -depth}},
                        {.position = {-4, 6, -depth}}, {.position = {4, 6, -depth}}};
    payload.indices = {0u, 1u, 2u, 1u, 3u, 2u};
    const auto asset = session.assets().add_mesh(std::move(mesh));
    const auto entity = session.scene().create_entity("Near-plane CPU occluder", EntityKind::Mesh);
    ASSERT_TRUE(session.scene().set_mesh(entity, {.mesh = asset}));
    SceneSurfaceQuery surfaces(session.scene(), session.assets(), session.viewport_local_visibility());
    const auto exact = surfaces.snapshot();
    const auto geometry = exact.selection_depth_geometry();
    const auto snapshot = harness.capture(geometry, camera);
    ASSERT_TRUE(snapshot);
    const auto cpu_hit = exact.nearest_hit({.world_origin = {0, 3.75f, 0}, .world_direction = {0, 0, -1},
                                          .eligibility = SceneSurfaceEligibility::RenderVisible});
    if (depth > 0.0f) {
      ASSERT_TRUE(cpu_hit);
      EXPECT_LT(cpu_hit->distance, 1.0f);
    }
    const auto decision = classify_viewport_selection_depth(*snapshot, {32, 20}, {0, 3.75f, 0},
        {0, 0, -1}, 1, 0.001,
        {.owner = &exact, .intersect = [](const void *owner, std::uint32_t triangle_id,
                                        QVec3 origin, QVec3 direction) {
          return static_cast<const SceneSurfaceQuerySnapshot *>(owner)
              ->selection_depth_triangle_distance(triangle_id, origin, direction);
        }});
    if (depth > -1.0f) {
      EXPECT_FALSE(snapshot->conservative_coverage);
      EXPECT_EQ(decision, ViewportSelectionDepthDecision::Overflow);
    } else {
      EXPECT_TRUE(snapshot->conservative_coverage);
      EXPECT_FALSE(cpu_hit);
      EXPECT_EQ(decision, ViewportSelectionDepthDecision::Accepted);
    }
  }
}

} // namespace
} // namespace quader

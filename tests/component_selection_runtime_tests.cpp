////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/component_selection_renderer.hpp"
#include "render/filament_material_library.hpp"
#include "render/png_texture_loader.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/viewport_scene_hit_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <gtest/gtest.h>
#include <QString>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

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

TEST(ComponentSelectionRuntimeTests,
     VulkanPartialVertexMarqueeMatchesCpuSelectionBeforeBackgroundWall) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_SELECTION_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_SELECTION_TESTS=1 for Vulkan component marquee coverage";
  }
  ComponentSelectionRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN));
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));

  quader_poly::Document wall_document;
  std::array<quader_poly::ElementId, 4> wall_vertices{};
  constexpr std::array kWallPositions{
      QVec3{-12.0f, -10.0f, -4.0f}, QVec3{12.0f, -10.0f, -4.0f},
      QVec3{12.0f, 10.0f, -4.0f}, QVec3{-12.0f, 10.0f, -4.0f}};
  for (std::size_t index = 0u; index < wall_vertices.size(); ++index) {
    wall_vertices[index] = quader_poly::add_vertex(wall_document, kWallPositions[index]);
  }
  (void)quader_poly::add_face(wall_document, wall_vertices);
  const auto wall = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(wall_document)),
      {.name = "Component Marquee Background Wall"});
  ASSERT_TRUE(wall.ok()) << wall.error_message();

  constexpr std::array kPositions{
      QVec3{-2.0f, -2.5f, -0.25f}, QVec3{2.0f, -2.5f, -0.25f},
      QVec3{2.0f, 2.5f, -0.25f}, QVec3{-2.0f, 2.5f, -0.25f},
      QVec3{-2.0f, -2.5f, 0.25f}, QVec3{2.0f, -2.5f, 0.25f},
      QVec3{2.0f, 2.5f, 0.25f}, QVec3{-2.0f, 2.5f, 0.25f}};
  constexpr std::array kFaces{
      std::array{4u, 5u, 6u, 7u}, std::array{0u, 3u, 2u, 1u},
      std::array{1u, 2u, 6u, 5u}, std::array{0u, 4u, 7u, 3u},
      std::array{3u, 7u, 6u, 2u}, std::array{0u, 1u, 5u, 4u}};
  quader_poly::Document slab_document;
  std::array<quader_poly::ElementId, 8> vertices{};
  for (std::size_t index = 0u; index < vertices.size(); ++index) {
    vertices[index] = quader_poly::add_vertex(slab_document, kPositions[index]);
  }
  for (const auto &face : kFaces) {
    (void)quader_poly::add_face(slab_document,
        std::array{vertices[face[0]], vertices[face[1]],
                   vertices[face[2]], vertices[face[3]]});
  }
  const auto slab = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(slab_document)),
      {.name = "Component Marquee Foreground Slab"});
  ASSERT_TRUE(slab.ok()) << slab.error_message();
  ASSERT_TRUE(editor.scene().select(slab.value().entity).ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().clear().ok());

  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({-0.35f, -0.3f, -1.0f});
  const auto modeling_camera = ViewportCameraAdapter::modeling_camera(
      camera, camera.viewport_size());
  const ViewportSceneProjectionCamera projection(modeling_camera);
  const auto upper_back = projection.project_world_point(kPositions[2]);
  const auto upper_front = projection.project_world_point(kPositions[6]);
  ASSERT_TRUE(upper_back.visible);
  ASSERT_TRUE(upper_front.visible);
  const QuaderBoxSelectRequest request{
      .camera = {.orthographic = modeling_camera.orthographic,
                 .origin = modeling_camera.origin,
                 .x_axis = modeling_camera.x_axis,
                 .y_axis = modeling_camera.y_axis,
                 .z_axis = modeling_camera.z_axis,
                 .viewport_size = modeling_camera.viewport_size,
                 .orthographic_size = modeling_camera.orthographic_size,
                 .fov_degrees = modeling_camera.fov_degrees},
      .start = {std::max(upper_back.position.x, upper_front.position.x) + 35.0f,
                std::max(upper_back.position.y, upper_front.position.y) + 80.0f},
      .end = {std::min(upper_back.position.x, upper_front.position.x) - 15.0f,
              std::min(upper_back.position.y, upper_front.position.y) - 15.0f},
      .rule = QuaderBoxSelectionRule::Crossing,
      .select_backfaces = false};
  const auto selected_vertices = [&] {
    std::vector<std::array<int, 2>> result;
    for (const auto &object : session.modeling().export_selection().objects) {
      for (const auto &vertex : object.selected_vertices) {
        result.push_back({object.modeling_object_id, static_cast<int>(vertex.id)});
      }
    }
    std::ranges::sort(result);
    return result;
  };
  std::vector<std::array<int, 2>> expected{
      {slab.value().modeling_object_id, static_cast<int>(vertices[2])},
      {slab.value().modeling_object_id, static_cast<int>(vertices[6])}};
  std::ranges::sort(expected);

  // The public API runs native projection, scene occlusion and publication.
  const auto cpu_selection = editor.selection().box_select(request);
  ASSERT_TRUE(cpu_selection.ok()) << cpu_selection.error_message();
  EXPECT_EQ(selected_vertices(), expected);
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto surfaces = session.scene_surface_query().snapshot();
  const auto geometry = surfaces.selection_depth_geometry();
  const auto depth_camera = projection.selection_depth_camera();
  const auto snapshot = harness.capture(geometry, depth_camera);
  ASSERT_TRUE(snapshot);
  ASSERT_TRUE(snapshot->conservative_coverage);
  session.scene_surface_query().set_selection_depth_source(harness.selection.source());
  ASSERT_EQ(session.scene_surface_query().snapshot().selection_depth_snapshot(depth_camera),
            snapshot);

  const auto before_gpu = session.scene_surface_query().stats().selection_depth;
  const auto gpu_selection = editor.selection().box_select(request);
  ASSERT_TRUE(gpu_selection.ok()) << gpu_selection.error_message();
  EXPECT_EQ(selected_vertices(), expected);
  const auto after_gpu = session.scene_surface_query().stats().selection_depth;
  EXPECT_EQ(after_gpu.gpu_accepted_witnesses - before_gpu.gpu_accepted_witnesses, 2u);
  EXPECT_EQ(after_gpu.gpu_rejected_witnesses, before_gpu.gpu_rejected_witnesses);
  EXPECT_EQ(after_gpu.cpu_fallback_witnesses, before_gpu.cpu_fallback_witnesses);

  // A retained readback for the old view must return to the exact CPU path.
  ASSERT_TRUE(editor.selection().clear().ok());
  auto moved_request = request;
  moved_request.camera.origin.x += 0.01f;
  const auto before_stale = session.scene_surface_query().stats().selection_depth;
  const auto stale_selection = editor.selection().box_select(moved_request);
  ASSERT_TRUE(stale_selection.ok()) << stale_selection.error_message();
  EXPECT_EQ(selected_vertices(), expected);
  const auto after_stale = session.scene_surface_query().stats().selection_depth;
  EXPECT_EQ(after_stale.stale_snapshot_queries - before_stale.stale_snapshot_queries, 1u);
  EXPECT_EQ(after_stale.cpu_fallback_witnesses - before_stale.cpu_fallback_witnesses, 2u);
  EXPECT_EQ(after_stale.gpu_accepted_witnesses, before_stale.gpu_accepted_witnesses);
  EXPECT_EQ(after_stale.gpu_rejected_witnesses, before_stale.gpu_rejected_witnesses);
}

TEST(ComponentSelectionRuntimeTests,
     VulkanHallwayMarqueeMatchesCpuAcrossRenderedRoomWallSidedness) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_SELECTION_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_SELECTION_TESTS=1 for Vulkan hallway coverage";
  }
  ComponentSelectionRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN));
  struct WallCase final {
    const char *name;
    bool reversed;
    bool double_sided;
    bool reflected;
    bool occludes;
  };
  constexpr std::array kWallCases{
      WallCase{"culled back face", false, false, false, false},
      WallCase{"front face", true, false, false, true},
      WallCase{"double-sided back face", false, true, false, true},
      WallCase{"reflected back face", false, false, true, true},
      WallCase{"reflected front face", true, false, true, false}};
  for (const bool complete_room : {true, false}) {
    SCOPED_TRACE(complete_room ? "saved room crossing camera plane"
                              : "bounded wall with certified GPU coverage");
    for (const auto &wall_case : kWallCases) {
      SCOPED_TRACE(wall_case.name);
      SceneSession session;
      QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
      // The reported scene's near room face and planar inset, including its
      // saved camera. The wall is nearer than both selected vertices; a wall
      // behind the target cannot detect the original occlusion regression.
      constexpr QVec3 kTranslation{-7.5f, 0.5f, -10.5f};
      constexpr std::array kRoomPositions{
          QVec3{-0.5f, 0.5f, -51.5f}, QVec3{-0.5f, 0.5f, -14.5f},
          QVec3{-0.5f, 0.5f, 14.5f}, QVec3{-0.5f, 11.5f, 14.5f},
          QVec3{-0.5f, 11.5f, -14.5f}, QVec3{-0.5f, 11.5f, -51.5f}};
      // The saved wall crosses the camera plane, which correctly requires the
      // CPU fallback. This bounded version covers the same two witness rays
      // and separately verifies sidedness using certified GPU candidates.
      constexpr std::array kBoundedRoomPositions{
          QVec3{-8.0f, 9.0f, -19.0f}, QVec3{-8.0f, 9.0f, -17.0f},
          QVec3{-8.0f, 9.0f, -15.0f}, QVec3{-8.0f, 12.0f, -15.0f},
          QVec3{-8.0f, 12.0f, -17.0f}, QVec3{-8.0f, 12.0f, -19.0f}};
      quader_poly::Document room_document;
      std::array<quader_poly::ElementId, 6> room_vertices{};
      for (std::size_t index = 0u; index < room_vertices.size(); ++index) {
        room_vertices[index] = quader_poly::add_vertex(
            room_document, complete_room ? kRoomPositions[index] + kTranslation
                                         : kBoundedRoomPositions[index]);
      }
      if (wall_case.reversed) {
        std::ranges::reverse(room_vertices);
      }
      (void)quader_poly::add_face(room_document, room_vertices);
      const auto room = editor.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(std::move(room_document)),
          {.name = "Near hallway room face"});
      ASSERT_TRUE(room.ok()) << room.error_message();
      ASSERT_TRUE(editor.scene().set_mesh_component(
          room.value().entity,
          {.mesh = room.value().mesh, .double_sided = wall_case.double_sided}).ok());
      if (wall_case.reflected) {
        // Reflect around the wall's vertical midpoint to reverse world winding
        // without moving its footprint away from the tested camera rays.
        ASSERT_TRUE(editor.scene().set_transform(
            room.value().entity,
            {.position = {0.0f, complete_room ? 13.0f : 21.0f, 0.0f},
             .scale = {1.0f, -1.0f, 1.0f}}).ok());
      }

      constexpr std::array kInsetPositions{
          QVec3{-12.5f, 10.5f, -8.5f}, QVec3{-12.5f, 1.49999976158f, -8.5f},
          QVec3{-12.5f, 1.49999976158f, -17.5f}, QVec3{-12.5f, 10.5f, -17.5f},
          QVec3{-12.5f, 11.5f, -18.5f}, QVec3{-12.5f, 11.5f, -7.5f},
          QVec3{-12.5f, 0.5f, -7.5f}, QVec3{-12.5f, 0.5f, -18.5f}};
      constexpr std::array kInsetFaces{
          std::array{0u, 1u, 2u, 3u}, std::array{4u, 5u, 0u, 3u},
          std::array{5u, 6u, 1u, 0u}, std::array{7u, 4u, 3u, 2u},
          std::array{6u, 7u, 2u, 1u}};
      quader_poly::Document inset_document;
      std::array<quader_poly::ElementId, 8> inset_vertices{};
      for (std::size_t index = 0u; index < inset_vertices.size(); ++index) {
        inset_vertices[index] = quader_poly::add_vertex(
            inset_document, kInsetPositions[index] + kTranslation);
      }
      for (const auto &face : kInsetFaces) {
        (void)quader_poly::add_face(inset_document,
            std::array{inset_vertices[face[0]], inset_vertices[face[1]],
                       inset_vertices[face[2]], inset_vertices[face[3]]});
      }
      const auto inset = editor.modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(std::move(inset_document)),
          {.name = "Hallway planar inset"});
      ASSERT_TRUE(inset.ok()) << inset.error_message();
      ASSERT_TRUE(editor.scene().select(inset.value().entity).ok());
      ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
      ASSERT_TRUE(editor.selection().clear().ok());

      ViewportCameraController camera;
      camera.set_viewport_size(1200.0f, 800.0f);
      ASSERT_TRUE(camera.restore_perspective_pose(
          {.eye = {-3.2057762146f, 10.1406784058f, -12.6346263885f},
           .target = {-38.0157394409f, 6.5449800491f, -35.6011009216f},
           .up = {-0.0717023462f, 0.9963036776f, -0.0473059975f}}));
      const auto modeling_camera = ViewportCameraAdapter::modeling_camera(
          camera, camera.viewport_size());
      const ViewportSceneProjectionCamera projection(modeling_camera);
      const QuaderBoxSelectRequest request{
          .camera = {.orthographic = modeling_camera.orthographic,
                     .origin = modeling_camera.origin,
                     .x_axis = modeling_camera.x_axis,
                     .y_axis = modeling_camera.y_axis,
                     .z_axis = modeling_camera.z_axis,
                     .viewport_size = modeling_camera.viewport_size,
                     .orthographic_size = modeling_camera.orthographic_size,
                     .fov_degrees = modeling_camera.fov_degrees},
          .start = {818.0f, 475.0f},
          .end = {658.0f, 220.0f},
          .rule = QuaderBoxSelectionRule::Crossing,
          .select_backfaces = false};
      using VertexKey = std::array<int, 2>;
      const auto selected_vertices = [&] {
        std::vector<VertexKey> result;
        for (const auto &object : session.modeling().export_selection().objects) {
          for (const auto &vertex : object.selected_vertices) {
            result.push_back({object.modeling_object_id, static_cast<int>(vertex.id)});
          }
        }
        std::ranges::sort(result);
        return result;
      };
      std::vector<VertexKey> projected_inside;
      for (std::size_t index = 0u; index < inset_vertices.size(); ++index) {
        const auto point = projection.project_world_point(kInsetPositions[index] + kTranslation);
        if (point.visible && point.position.x >= request.end.x &&
            point.position.x <= request.start.x && point.position.y >= request.end.y &&
            point.position.y <= request.start.y) {
          projected_inside.push_back(
              {inset.value().modeling_object_id, static_cast<int>(inset_vertices[index])});
        }
      }
      std::vector<VertexKey> unoccluded{
          {inset.value().modeling_object_id, static_cast<int>(inset_vertices[3])},
          {inset.value().modeling_object_id, static_cast<int>(inset_vertices[4])}};
      std::ranges::sort(unoccluded);
      std::ranges::sort(projected_inside);
      ASSERT_EQ(projected_inside, unoccluded);
      const std::vector<VertexKey> expected =
          wall_case.occludes ? std::vector<VertexKey>{} : unoccluded;

      const auto vertex_screen = projection.project_world_point(
          kInsetPositions[3] + kTranslation);
      const auto vertex_ray = projection.ray_from_viewport_point(vertex_screen.position);
      const auto point_selection = editor.selection().pick_select(
          {.ray = {.origin = vertex_ray.origin, .direction = vertex_ray.direction},
           .select_backfaces = false});
      ASSERT_TRUE(point_selection.ok()) << point_selection.error_message();
      const std::vector<VertexKey> expected_point = wall_case.occludes
          ? std::vector<VertexKey>{}
          : std::vector<VertexKey>{{inset.value().modeling_object_id,
                                    static_cast<int>(inset_vertices[3])}};
      EXPECT_EQ(selected_vertices(), expected_point);
      ASSERT_TRUE(editor.selection().clear().ok());

      const auto cpu_selection = editor.selection().box_select(request);
      ASSERT_TRUE(cpu_selection.ok()) << cpu_selection.error_message();
      EXPECT_EQ(selected_vertices(), expected);
      ASSERT_TRUE(editor.selection().clear().ok());
      const auto surfaces = session.scene_surface_query().snapshot();
      const auto depth_camera = projection.selection_depth_camera();
      const auto geometry = surfaces.selection_depth_geometry();
      const auto before_capture = harness.selection.stats();
      const auto snapshot = harness.capture(geometry, depth_camera);
      ASSERT_TRUE(snapshot);
      ASSERT_EQ(snapshot->conservative_coverage, !complete_room);
      EXPECT_GT(harness.selection.stats().completed_readbacks,
                before_capture.completed_readbacks);
      session.scene_surface_query().set_selection_depth_source(harness.selection.source());
      ASSERT_EQ(session.scene_surface_query().snapshot().selection_depth_snapshot(depth_camera),
                snapshot);

      const auto before_gpu = session.scene_surface_query().stats().selection_depth;
      const auto gpu_selection = editor.selection().box_select(request);
      ASSERT_TRUE(gpu_selection.ok()) << gpu_selection.error_message();
      EXPECT_EQ(selected_vertices(), expected);
      const auto after_gpu = session.scene_surface_query().stats().selection_depth;
      const auto accepted = after_gpu.gpu_accepted_witnesses -
                            before_gpu.gpu_accepted_witnesses;
      const auto rejected = after_gpu.gpu_rejected_witnesses -
                            before_gpu.gpu_rejected_witnesses;
      const auto fallback = after_gpu.cpu_fallback_witnesses -
                            before_gpu.cpu_fallback_witnesses;
      EXPECT_EQ(accepted + rejected + fallback, 2u);
      if (wall_case.occludes || complete_room) {
        EXPECT_EQ(accepted, 0u);
      }
      if (!wall_case.occludes) {
        EXPECT_EQ(rejected, 0u);
      }
      if (!complete_room && wall_case.occludes) {
        EXPECT_EQ(rejected, 2u);
        EXPECT_EQ(fallback, 0u);
      }
      if (!complete_room && !wall_case.occludes && !wall_case.reflected) {
        EXPECT_GT(accepted, 0u);
      }
      // Coincident boundary triangles can exhaust the bounded GPU peel layers;
      // those witnesses must use the exact CPU oracle rather than disappear.
      EXPECT_EQ(after_gpu.gpu_overflow_witnesses - before_gpu.gpu_overflow_witnesses,
                fallback);

      // Through-selection deliberately bypasses rendered occlusion. It cannot
      // silently enter the visible-selection path when that path rejects both.
      ASSERT_TRUE(editor.selection().clear().ok());
      auto through_request = request;
      through_request.select_backfaces = true;
      const auto through_selection = editor.selection().box_select(through_request);
      ASSERT_TRUE(through_selection.ok()) << through_selection.error_message();
      EXPECT_EQ(selected_vertices(), unoccluded);
      const auto after_through = session.scene_surface_query().stats().selection_depth;
      EXPECT_EQ(after_through.gpu_accepted_witnesses, after_gpu.gpu_accepted_witnesses);
      EXPECT_EQ(after_through.gpu_rejected_witnesses, after_gpu.gpu_rejected_witnesses);
      EXPECT_EQ(after_through.cpu_fallback_witnesses, after_gpu.cpu_fallback_witnesses);
    }
  }
}

} // namespace
} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_material_library.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/selected_mesh_outline_mask_cache.hpp"

#include <gtest/gtest.h>

#include <filament/Box.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/Entity.h>

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

namespace quader {
namespace {

/// Keeps sub-unit geometry measurable even far beyond float translation range.
struct MaskWorldBounds final {
  filament::math::double3 center;
  filament::math::double3 half_extent;

  filament::math::double3 maximum() const { return center + half_extent; }
};

/// Runs retained production resources without a window; Vulkan is opt-in.
class SelectedMeshOutlineMaskHarness final {
public:
  ~SelectedMeshOutlineMaskHarness() {
    if (engine_ == nullptr) return;
    (void)engine_->flushAndWait(10'000'000'000ULL);
    engine_->pumpMessageQueues();
    cache_.shutdown();
    materials_.shutdown();
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
    const QString resource_root = QCoreApplication::instance() != nullptr
        ? QCoreApplication::applicationDirPath()
        : qEnvironmentVariable("QT_PLUGIN_PATH");
    if (!materials_.initialize(
            *engine_, std::filesystem::path(resource_root.toStdWString()))) {
      return false;
    }
    return cache_.initialize(*engine_, materials_.selected_mesh_outline_mask());
  }

  SelectedMeshOutlineMaskCache &cache() { return cache_; }

  filament::Box only_local_geometry_bounds() const {
    const auto &manager = engine_->getRenderableManager();
    EXPECT_EQ(manager.getComponentCount(), 1u);
    if (manager.getComponentCount() != 1u) return {};
    return manager.getAxisAlignedBoundingBox(
        manager.getInstance(manager.getEntities()[0]));
  }

  filament::math::mat4 only_world_transform() const {
    const auto &manager = engine_->getRenderableManager();
    EXPECT_EQ(manager.getComponentCount(), 1u);
    if (manager.getComponentCount() != 1u) return {};
    const auto &transforms = engine_->getTransformManager();
    const auto transform = transforms.getInstance(manager.getEntities()[0]);
    EXPECT_TRUE(transform.isValid());
    return transform.isValid() ? transforms.getWorldTransformAccurate(transform)
                               : filament::math::mat4{};
  }

  MaskWorldBounds only_geometry_bounds() const {
    const auto bounds = only_local_geometry_bounds();
    const auto transform = only_world_transform();
    const auto local_minimum = bounds.getMin();
    const auto local_maximum = bounds.getMax();
    filament::math::double3 minimum{std::numeric_limits<double>::max()};
    filament::math::double3 maximum{std::numeric_limits<double>::lowest()};
    for (unsigned int corner = 0u; corner < 8u; ++corner) {
      const filament::math::double4 local{
          (corner & 1u) != 0u ? local_maximum.x : local_minimum.x,
          (corner & 2u) != 0u ? local_maximum.y : local_minimum.y,
          (corner & 4u) != 0u ? local_maximum.z : local_minimum.z, 1.0};
      const auto world = transform * local;
      for (std::size_t axis = 0u; axis < 3u; ++axis) {
        minimum[axis] = std::min(minimum[axis], world[axis]);
        maximum[axis] = std::max(maximum[axis], world[axis]);
      }
    }
    return {.center = (minimum + maximum) * 0.5,
            .half_extent = (maximum - minimum) * 0.5};
  }

private:
  filament::Engine *engine_ = nullptr;
  FilamentMaterialLibrary materials_;
  SelectedMeshOutlineMaskCache cache_;
};

std::shared_ptr<const MeshGeometryPayload> triangle_payload(float tip_y = 1.0f) {
  auto payload = std::make_shared<MeshGeometryPayload>();
  payload->vertices = {
      {.position = {-1.0f, -1.0f, 0.0f}},
      {.position = {1.0f, -1.0f, 0.0f}},
      {.position = {0.0f, tip_y, 0.0f}},
  };
  payload->indices = {0u, 1u, 2u};
  return payload;
}

std::shared_ptr<const MeshGeometryPayload> box_payload() {
  auto payload = std::make_shared<MeshGeometryPayload>();
  payload->vertices = {
      {.position = {-0.5f, -0.5f, -0.5f}},
      {.position = {0.5f, -0.5f, -0.5f}},
      {.position = {0.5f, 0.5f, -0.5f}},
      {.position = {-0.5f, 0.5f, -0.5f}},
      {.position = {-0.5f, -0.5f, 0.5f}},
      {.position = {0.5f, -0.5f, 0.5f}},
      {.position = {0.5f, 0.5f, 0.5f}},
      {.position = {-0.5f, 0.5f, 0.5f}},
  };
  payload->indices = {
      0u, 2u, 1u, 0u, 3u, 2u, 4u, 5u, 6u, 4u, 6u, 7u,
      0u, 1u, 5u, 0u, 5u, 4u, 3u, 7u, 6u, 3u, 6u, 2u,
      0u, 4u, 7u, 0u, 7u, 3u, 1u, 2u, 6u, 1u, 6u, 5u,
  };
  return payload;
}

RenderOverlayMeshSnapshot selected_mesh(
    std::shared_ptr<const MeshGeometryPayload> payload, EntityId entity = 1u,
    bool active = false) {
  RenderOverlayMeshSnapshot mesh;
  mesh.entity = entity;
  mesh.mesh_id = 1u;
  mesh.mesh_revision = 1u;
  mesh.geometry_payload = std::move(payload);
  mesh.geometry_content_id = mesh.geometry_payload->content_id();
  mesh.selected = true;
  mesh.active_selected = active;
  mesh.vertices = mesh.geometry_payload->vertices;
  mesh.indices = mesh.geometry_payload->indices;
  return mesh;
}

void expect_no_work_since(const SelectedMeshOutlineMaskCache &cache,
                          const SelectedMeshOutlineMaskWorkStats &before) {
  const auto after = cache.work_stats();
  EXPECT_EQ(after.source_refreshes, before.source_refreshes);
  EXPECT_EQ(after.geometry_builds, before.geometry_builds);
  EXPECT_EQ(after.uploaded_vertices, before.uploaded_vertices);
  EXPECT_EQ(after.uploaded_indices, before.uploaded_indices);
  EXPECT_EQ(after.command_submissions, before.command_submissions);
  EXPECT_EQ(after.ordinary_batches, before.ordinary_batches);
  EXPECT_EQ(after.active_batches, before.active_batches);
}

TEST(SelectedMeshOutlineMaskCacheTests,
     ThousandsOfBoxOccurrencesStayBatchedAndUnchangedFramesDoNoWork) {
  SelectedMeshOutlineMaskHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto &cache = harness.cache();
  constexpr std::size_t kOccurrenceCount = 6400u;
  const auto payload = box_payload();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.selection_revision = 1u;
  snapshot.meshes.reserve(kOccurrenceCount);
  for (std::size_t index = 0u; index < kOccurrenceCount; ++index) {
    auto mesh = selected_mesh(payload, static_cast<EntityId>(index + 1u),
                              index == 0u);
    mesh.world_transform.values[12] = static_cast<float>(index % 80u) * 2.0f;
    mesh.world_transform.values[13] = static_cast<float>(index / 80u) * 2.0f;
    snapshot.meshes.push_back(std::move(mesh));
  }

  ASSERT_TRUE(cache.sync(&snapshot));
  ASSERT_NE(cache.scene(), nullptr);
  const auto populated = cache.work_stats();
  EXPECT_TRUE(cache.has_geometry(false));
  EXPECT_TRUE(cache.has_geometry(true));
  EXPECT_GT(populated.ordinary_batches, 0u);
  EXPECT_GT(populated.active_batches, 0u);
  EXPECT_LT(populated.ordinary_batches + populated.active_batches, 10u);
  EXPECT_EQ(cache.scene()->getRenderableCount(),
            populated.ordinary_batches + populated.active_batches);
  EXPECT_EQ(populated.uploaded_indices, kOccurrenceCount * 36u);
  EXPECT_GE(populated.uploaded_vertices, kOccurrenceCount * 8u);
  EXPECT_LE(populated.uploaded_vertices, kOccurrenceCount * 36u);

  // Camera clipping, presentation width, color and raster scale do not alter
  // the selected triangles or require reinspection of thousands of sources.
  for (int frame = 0; frame < 100; ++frame) {
    snapshot.clip_planes.near_plane = 0.01f + static_cast<float>(frame) * 0.01f;
    snapshot.style.raster_scale = frame % 2 == 0 ? 1.0f : 2.0f;
    snapshot.style.selection_edge_line_width_pixels =
        1.0f + static_cast<float>(frame % 4);
    snapshot.style.selection_wire_color ^= 0x00010100u;
    ++snapshot.style.wire_signature;
    EXPECT_FALSE(cache.sync(&snapshot));
  }
  expect_no_work_since(cache, populated);
}

TEST(SelectedMeshOutlineMaskCacheTests,
     TransformPreviewPayloadAndActiveClassChangesRefreshRetainedGeometry) {
  SelectedMeshOutlineMaskHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto &cache = harness.cache();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.selection_revision = 1u;
  snapshot.meshes.push_back(selected_mesh(triangle_payload()));
  ASSERT_TRUE(cache.sync(&snapshot));
  auto previous = cache.work_stats();

  snapshot.meshes.front().world_transform.values[12] = 4.0f;
  ++snapshot.transform_revision;
  ASSERT_TRUE(cache.sync(&snapshot));
  auto refreshed = cache.work_stats();
  EXPECT_GT(refreshed.geometry_builds, previous.geometry_builds);
  EXPECT_EQ(refreshed.uploaded_indices - previous.uploaded_indices, 3u);
  EXPECT_FLOAT_EQ(harness.only_geometry_bounds().center.x, 4.0f);
  previous = refreshed;

  // Live preview publication can replace immutable geometry while the
  // committed asset revision and all document stamps remain unchanged.
  auto preview_meshes = snapshot.meshes;
  preview_meshes.front() = selected_mesh(triangle_payload(2.0f));
  preview_meshes.front().world_transform = snapshot.meshes.front().world_transform;
  ASSERT_EQ(preview_meshes.front().mesh_revision,
            snapshot.meshes.front().mesh_revision);
  ASSERT_NE(preview_meshes.front().geometry_content_id,
            snapshot.meshes.front().geometry_content_id);
  snapshot.meshes.swap(preview_meshes);
  ASSERT_TRUE(cache.sync(&snapshot));
  refreshed = cache.work_stats();
  EXPECT_GT(refreshed.geometry_builds, previous.geometry_builds);
  EXPECT_EQ(refreshed.uploaded_indices - previous.uploaded_indices, 3u);
  EXPECT_DOUBLE_EQ(harness.only_geometry_bounds().maximum().y, 2.0);
  EXPECT_FLOAT_EQ(harness.only_geometry_bounds().center.x, 4.0f);
  previous = refreshed;

  snapshot.meshes.front().active_selected = true;
  ++snapshot.selection_revision;
  ASSERT_TRUE(cache.sync(&snapshot));
  refreshed = cache.work_stats();
  EXPECT_GT(refreshed.geometry_builds, previous.geometry_builds);
  EXPECT_FALSE(cache.has_geometry(false));
  EXPECT_TRUE(cache.has_geometry(true));
  EXPECT_EQ(refreshed.ordinary_batches, 0u);
  EXPECT_EQ(refreshed.active_batches, 1u);
  EXPECT_FALSE(cache.sync(&snapshot));
  expect_no_work_since(cache, refreshed);
}

TEST(SelectedMeshOutlineMaskCacheTests,
     HiddenAndMissingSourcesRetireGeometryWithoutRepeatedSubmissions) {
  SelectedMeshOutlineMaskHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto &cache = harness.cache();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.selection_revision = 1u;
  snapshot.meshes.push_back(selected_mesh(triangle_payload()));
  ASSERT_TRUE(cache.sync(&snapshot));
  ASSERT_TRUE(cache.has_geometry(false));

  snapshot.visible = false;
  EXPECT_TRUE(cache.sync(&snapshot));
  EXPECT_FALSE(cache.has_geometry(false));
  EXPECT_FALSE(cache.has_geometry(true));
  EXPECT_EQ(cache.scene()->getRenderableCount(), 0u);
  const auto hidden = cache.work_stats();
  EXPECT_FALSE(cache.sync(&snapshot));
  expect_no_work_since(cache, hidden);

  snapshot.visible = true;
  ASSERT_TRUE(cache.sync(&snapshot));
  ASSERT_TRUE(cache.has_geometry(false));
  EXPECT_TRUE(cache.sync(nullptr));
  EXPECT_FALSE(cache.has_geometry(false));
  EXPECT_FALSE(cache.has_geometry(true));
  EXPECT_EQ(cache.scene()->getRenderableCount(), 0u);
  const auto empty = cache.work_stats();
  EXPECT_FALSE(cache.sync(nullptr));
  expect_no_work_since(cache, empty);
}

TEST(SelectedMeshOutlineMaskCacheTests,
     InvalidIndicesAndNonfinitePositionsNeverBecomeMaskGeometry) {
  SelectedMeshOutlineMaskHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto &cache = harness.cache();
  const auto payload = triangle_payload();
  const std::array<std::uint32_t, 3u> invalid_indices{0u, 1u, 99u};
  auto nonfinite_payload = std::make_shared<MeshGeometryPayload>(*payload);
  nonfinite_payload->vertices.front().position.x =
      std::numeric_limits<float>::quiet_NaN();
  nonfinite_payload->invalidate_content_id();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.selection_revision = 1u;
  snapshot.meshes.push_back(selected_mesh(payload, 1u));
  auto invalid_mesh = selected_mesh(payload, 2u, true);
  invalid_mesh.indices = invalid_indices;
  snapshot.meshes.push_back(std::move(invalid_mesh));
  snapshot.meshes.push_back(selected_mesh(nonfinite_payload, 3u, true));
  auto invalid_transform = selected_mesh(payload, 4u, true);
  invalid_transform.world_transform.values[12] =
      std::numeric_limits<float>::infinity();
  snapshot.meshes.push_back(std::move(invalid_transform));

  ASSERT_TRUE(cache.sync(&snapshot));
  const auto populated = cache.work_stats();
  EXPECT_EQ(populated.uploaded_indices, 3u);
  EXPECT_EQ(populated.uploaded_vertices, 3u);
  EXPECT_EQ(populated.ordinary_batches, 1u);
  EXPECT_EQ(populated.active_batches, 0u);
  EXPECT_EQ(cache.scene()->getRenderableCount(), 1u);
  EXPECT_TRUE(cache.has_geometry(false));
  EXPECT_FALSE(cache.has_geometry(true));
}

TEST(SelectedMeshOutlineMaskCacheTests,
     StandaloneHierarchyLeavesRetainTheirActiveClassAndTransform) {
  SelectedMeshOutlineMaskHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto &cache = harness.cache();
  RenderOverlaySelectionSnapshot hierarchy;
  hierarchy.selection_revision = 1u;
  auto leaf = selected_mesh(triangle_payload(), 10u, true);
  leaf.selection_root = 9u;
  hierarchy.hierarchy_selection_wires.push_back(std::move(leaf));

  ASSERT_TRUE(cache.sync(nullptr, &hierarchy));
  EXPECT_FALSE(cache.has_geometry(false));
  EXPECT_TRUE(cache.has_geometry(true));
  const auto before = cache.work_stats();
  EXPECT_EQ(before.uploaded_indices, 3u);
  EXPECT_FALSE(cache.sync(nullptr, &hierarchy));
  expect_no_work_since(cache, before);

  ++hierarchy.transform_revision;
  hierarchy.hierarchy_selection_wires.front().world_transform.values[13] = 3.0f;
  EXPECT_TRUE(cache.sync(nullptr, &hierarchy));
  EXPECT_GT(cache.work_stats().geometry_builds, before.geometry_builds);
  EXPECT_DOUBLE_EQ(harness.only_geometry_bounds().maximum().y, 4.0);
  EXPECT_TRUE(cache.sync(nullptr));
  EXPECT_EQ(cache.scene()->getRenderableCount(), 0u);
}

TEST(SelectedMeshOutlineMaskCacheTests,
     DirectHierarchyAndInstanceTrianglesShareBatchesWithoutDuplicateWires) {
  SelectedMeshOutlineMaskHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto &cache = harness.cache();
  const auto payload = triangle_payload();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.selection_revision = 1u;
  snapshot.meshes.push_back(selected_mesh(payload, 1u));
  QMat4 instance_transform;
  instance_transform.values[12] = 5.0f;
  snapshot.instance_selection_fills.push_back({
      .occurrence = 20u,
      .definition = 21u,
      .member = 22u,
      .mesh_id = 1u,
      .mesh_revision = 1u,
      .geometry_payload = payload,
      .geometry_content_id = payload->content_id(),
      .world_transform = instance_transform,
      .vertices = payload->vertices,
      .indices = payload->indices,
  });
  snapshot.instance_selection_wires.push_back({
      .occurrence = 20u,
      .definition = 21u,
      .member = 22u,
      .mesh_id = 1u,
      .mesh_revision = 1u,
      .geometry_payload = payload,
      .geometry_content_id = payload->content_id(),
      .world_transform = instance_transform,
      .vertices = payload->vertices,
      .indices = payload->indices,
  });
  RenderOverlaySelectionSnapshot hierarchy;
  hierarchy.selection_revision = 1u;
  auto leaf = selected_mesh(payload, 31u, true);
  leaf.selection_root = 30u;
  hierarchy.hierarchy_selection_wires.push_back(std::move(leaf));

  ASSERT_TRUE(cache.sync(&snapshot, &hierarchy));
  const auto populated = cache.work_stats();
  EXPECT_EQ(populated.uploaded_vertices, 9u);
  EXPECT_EQ(populated.uploaded_indices, 9u);
  EXPECT_EQ(populated.ordinary_batches, 1u);
  EXPECT_EQ(populated.active_batches, 1u);
  EXPECT_EQ(cache.scene()->getRenderableCount(), 2u);

  // A source publication unrelated to mask positions may refresh source facts,
  // but retains all buffers, renderables and queued GPU work.
  ++snapshot.material_catalog_revision;
  EXPECT_FALSE(cache.sync(&snapshot, &hierarchy));
  const auto refreshed = cache.work_stats();
  EXPECT_GT(refreshed.source_refreshes, populated.source_refreshes);
  EXPECT_EQ(refreshed.geometry_builds, populated.geometry_builds);
  EXPECT_EQ(refreshed.uploaded_indices, populated.uploaded_indices);
  EXPECT_EQ(refreshed.uploaded_vertices, populated.uploaded_vertices);
  EXPECT_EQ(refreshed.command_submissions, populated.command_submissions);
  EXPECT_EQ(cache.scene()->getRenderableCount(), 2u);
}

TEST(SelectedMeshOutlineMaskCacheTests,
     SmallTrianglesKeepTheirExtentsAtLargeWorldTranslations) {
  SelectedMeshOutlineMaskHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto &cache = harness.cache();
  RenderOverlaySelectedObjectSnapshot snapshot;
  snapshot.selection_revision = 1u;
  snapshot.meshes.push_back(selected_mesh(triangle_payload()));
  snapshot.meshes.front().world_transform.values[12] = 100'000'000.0f;

  ASSERT_TRUE(cache.sync(&snapshot));
  ASSERT_TRUE(cache.has_geometry(false));
  EXPECT_EQ(cache.work_stats().uploaded_indices, 3u);
  const auto local = harness.only_local_geometry_bounds();
  EXPECT_FLOAT_EQ(local.getMax().x - local.getMin().x, 2.0f);
  EXPECT_FLOAT_EQ(local.getMax().y - local.getMin().y, 2.0f);
  const auto world = harness.only_geometry_bounds();
  EXPECT_DOUBLE_EQ(world.center.x, 100'000'000.0);
  EXPECT_DOUBLE_EQ(world.half_extent.x, 1.0);
  EXPECT_DOUBLE_EQ(world.maximum().x, 100'000'001.0);
  EXPECT_NEAR(harness.only_world_transform()[3].x, 100'000'000.0, 2.0);

  const auto before = cache.work_stats();
  snapshot.meshes.front().world_transform.values[12] += 32.0f;
  ++snapshot.transform_revision;
  ASSERT_TRUE(cache.sync(&snapshot));
  const auto translated = harness.only_geometry_bounds();
  EXPECT_DOUBLE_EQ(translated.center.x, 100'000'032.0);
  EXPECT_DOUBLE_EQ(translated.half_extent.x, 1.0);
  EXPECT_EQ(cache.work_stats().uploaded_indices - before.uploaded_indices, 3u);
  EXPECT_FALSE(cache.sync(&snapshot));
  EXPECT_DOUBLE_EQ(harness.only_geometry_bounds().center.x, translated.center.x);
}

TEST(SelectedMeshOutlineMaskCacheTests,
     ReadinessTracksInitializedResourcesIndependentOfGeometry) {
  SelectedMeshOutlineMaskHarness harness;
  auto &cache = harness.cache();
  EXPECT_FALSE(cache.ready());
  ASSERT_TRUE(harness.initialize());
  EXPECT_TRUE(cache.ready());
  EXPECT_FALSE(cache.has_geometry(false));
  EXPECT_FALSE(cache.has_geometry(true));
  EXPECT_FALSE(cache.sync(nullptr));
  EXPECT_TRUE(cache.ready());
  cache.shutdown();
  EXPECT_FALSE(cache.ready());
  EXPECT_EQ(cache.scene(), nullptr);
  EXPECT_FALSE(cache.sync(nullptr));
}

} // namespace
} // namespace quader

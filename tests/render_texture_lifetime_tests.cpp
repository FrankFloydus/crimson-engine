////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "render/filament_material_library.hpp"
#include "render/material_binding_service.hpp"
#include "render/instance_renderable_cache.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/overlay/instanced_wire_render_backend.hpp"
#include "render/png_texture_loader.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_sync_service.hpp"
#include "render/texture_binding_service.hpp"
#include "support/texture_lifetime_harness.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QTemporaryDir>

#include <filament/Camera.h>
#include <filament/LightManager.h>
#include <filament/Renderer.h>
#include <filament/RenderableManager.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <backend/PixelBufferDescriptor.h>
#include <utils/EntityManager.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <iostream>
#include <unordered_set>
#include <vector>

namespace quader {
namespace {

using test::TextureLifetimeHarness;

/// Owns immutable image and geometry inputs for render-cache lifecycle tests.
class TexturedTriangle final {
public:
  explicit TexturedTriangle(
      const char *texture_slot = material_texture_slot::kBaseColor) {
    record.entity_id = 1u;
    record.mesh = {.mesh_id = 1u,
                   .mesh_revision = 1u,
                   .vertices = vertices_,
                   .indices = indices_,
                   .present = true};
    record.material.present = true;
    record.material.primary_material.present = true;
    record.material_reference = MaterialReference::project(*ProjectAssetId::parse(
        "14f96223-6a07-4ee9-9e12-845205d6d4f7"));
    record.material.component.material = record.material_reference;
    record.material.primary_material.reference = record.material_reference;
    RenderTextureAssetSnapshot texture;
    texture.slot = texture_slot;
    texture.project_texture_id = *ProjectAssetId::parse(
        "f7097612-7f3b-481b-98cb-5dce4a983aec");
    texture.texture.color_space =
        texture.slot == material_texture_slot::kNormal
            ? TextureColorSpace::Linear
            : TextureColorSpace::Srgb;
    texture.width = 2u;
    texture.height = 2u;
    texture.present = true;
    record.material.primary_material.textures.push_back(std::move(texture));
    replace_image(1u);
  }

  void change_scalar() {
    ++record.material_revision;
    record.material.revision = record.material_revision;
    record.material.primary_material.revision = record.material_revision;
    record.material.primary_material.document.roughness =
        static_cast<float>(record.material_revision % 10u) / 10.0f;
  }

  void replace_image(std::uint64_t image_revision) {
    RenderTextureAssetSnapshot &texture =
        record.material.primary_material.textures.front();
    texture.content_revision = image_revision;
    auto pixels = std::make_shared<std::vector<std::byte>>(16u);
    for (std::size_t pixel = 0u; pixel < 4u; ++pixel) {
      (*pixels)[pixel * 4u] = std::byte(image_revision % 256u);
      (*pixels)[pixel * 4u + 1u] = std::byte{128u};
      (*pixels)[pixel * 4u + 2u] = std::byte{64u};
      (*pixels)[pixel * 4u + 3u] = std::byte{255u};
    }
    texture.decoded_rgba8 = std::move(pixels);
    change_scalar();
  }

  RenderSceneMeshRecord record;

private:
  std::array<MeshVertex, 3u> vertices_{
      MeshVertex{.position = {-1.0f, -1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {0.0f, 0.0f}},
      MeshVertex{.position = {1.0f, -1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {1.0f, 0.0f}},
      MeshVertex{.position = {0.0f, 1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {0.5f, 1.0f}}};
  std::array<std::uint32_t, 3u> indices_{0u, 1u, 2u};
};

TEST(RenderTextureLifetimeTests,
     SolidTextureCenterMatchesTheGeneratedNeutralGrayFallback) {
  const std::filesystem::path texture_path =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "textures" /
      "default_texture.png";
  const QImage texture(QString::fromStdWString(texture_path.wstring()));
  ASSERT_FALSE(texture.isNull());
  ASSERT_GE(texture.width(), 2);
  ASSERT_GE(texture.height(), 2);

  const QColor neutral_gray(181, 181, 181, 255);
  const int center_left = (texture.width() - 1) / 2;
  const int center_right = texture.width() / 2;
  const int center_top = (texture.height() - 1) / 2;
  const int center_bottom = texture.height() / 2;
  for (const QPoint sample : {QPoint(center_left, center_top),
                              QPoint(center_right, center_top),
                              QPoint(center_left, center_bottom),
                              QPoint(center_right, center_bottom)}) {
    EXPECT_EQ(texture.pixelColor(sample), neutral_gray);
  }
  EXPECT_NE(texture.pixelColor(0, 0), neutral_gray)
      << "Solid must sample the neutral center instead of the authored border";
}

TEST(RenderTextureLifetimeTests,
     DiffuseModelUniformFollowsEmbeddedAndProjectedMaterialValues) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  AssetRegistry assets;
  for (const auto model : {MaterialDiffuseModel::Burley,
                           MaterialDiffuseModel::Lambert,
                           static_cast<MaterialDiffuseModel>(255u)}) {
    SCOPED_TRACE(static_cast<int>(model));
    MaterialDocument material;
    material.diffuse_model = model;
    const int expected = model == MaterialDiffuseModel::Lambert ? 1 : 0;
    filament::MaterialInstance *embedded =
        harness.materials().create_mesh_material_instance(assets, &material,
                                                          false);
    ASSERT_NE(embedded, nullptr);
    EXPECT_EQ(embedded->getParameter<int>("diffuseModel"), expected);
    harness.materials().destroy_mesh_material_instance(harness.engine(),
                                                       embedded);

    RenderMaterialAssetSnapshot snapshot;
    snapshot.present = true;
    snapshot.document = material;
    filament::MaterialInstance *projected =
        harness.materials().create_mesh_material_instance(&snapshot, false);
    ASSERT_NE(projected, nullptr);
    EXPECT_EQ(projected->getParameter<int>("diffuseModel"), expected);
    harness.materials().destroy_mesh_material_instance(harness.engine(),
                                                       projected);
  }

  filament::MaterialInstance *missing_embedded =
      harness.materials().create_mesh_material_instance(assets, nullptr, false);
  ASSERT_NE(missing_embedded, nullptr);
  EXPECT_EQ(missing_embedded->getParameter<int>("diffuseModel"), 0);
  harness.materials().destroy_mesh_material_instance(harness.engine(),
                                                     missing_embedded);
  RenderMaterialAssetSnapshot missing;
  missing.document.diffuse_model = MaterialDiffuseModel::Lambert;
  filament::MaterialInstance *missing_projected =
      harness.materials().create_mesh_material_instance(&missing, false);
  ASSERT_NE(missing_projected, nullptr);
  EXPECT_EQ(missing_projected->getParameter<int>("diffuseModel"), 0);
  harness.materials().destroy_mesh_material_instance(harness.engine(),
                                                     missing_projected);
}

TEST(RenderTextureLifetimeTests,
     SolidColorTextureIsBorrowedAndExcludedFromAssetCacheStats) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());

  filament::Texture *const solid_color =
      harness.textures().solid_color_texture();
  ASSERT_NE(solid_color, nullptr);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().material_consumer_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, 0u);
  EXPECT_EQ(harness.stats().destroy_count, 0u);

  AssetRegistry assets;
  filament::MaterialInstance *instance =
      harness.materials().create_mesh_material_instance(assets, nullptr, true,
                                                        true);
  ASSERT_NE(instance, nullptr);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().material_consumer_count, 0u)
      << "the app-owned Solid sampler is not a document material pin";
  harness.materials().destroy_mesh_material_instance(harness.engine(),
                                                     instance);
  harness.materials().bind_scene({.scene_session_id = 2u});
  EXPECT_EQ(harness.textures().solid_color_texture(), solid_color);
  EXPECT_EQ(harness.stats().resource_count, 0u);
}

TEST(RenderTextureLifetimeTests,
     ShadedNormalBindingsUseMaterialSettingsAndNeutralFallbacks) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  AssetRegistry assets;
  for (const bool editor_unlit : {true, false}) {
    SCOPED_TRACE(editor_unlit);
    filament::MaterialInstance *missing =
        harness.materials().create_mesh_material_instance(assets, nullptr, false,
                                                          editor_unlit);
    ASSERT_NE(missing, nullptr);
    EXPECT_FLOAT_EQ(missing->getParameter<float>("normalStrength"), 1.0f);
    EXPECT_FLOAT_EQ(missing->getParameter<float>("normalYSign"), 1.0f);
    EXPECT_EQ(harness.stats().resource_count, 0u);
    EXPECT_EQ(harness.stats().material_consumer_count, 0u);
    harness.materials().destroy_mesh_material_instance(harness.engine(), missing);

    RenderMaterialAssetSnapshot snapshot;
    snapshot.present = true;
    snapshot.normal_strength = 2.5f;
    snapshot.directx_normal = true;
    filament::MaterialInstance *projected =
        harness.materials().create_mesh_material_instance(&snapshot, false,
                                                          editor_unlit);
    ASSERT_NE(projected, nullptr);
    EXPECT_FLOAT_EQ(projected->getParameter<float>("normalStrength"), 2.5f);
    EXPECT_FLOAT_EQ(projected->getParameter<float>("normalYSign"), -1.0f);
    EXPECT_EQ(harness.stats().resource_count, 0u);
    EXPECT_EQ(harness.stats().material_consumer_count, 0u);
    harness.materials().destroy_mesh_material_instance(harness.engine(),
                                                       projected);
  }

  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const QString image_path = directory.filePath(QStringLiteral("normal.png"));
  QImage normal_image(2, 2, QImage::Format_RGBA8888);
  normal_image.fill(QColor(180, 128, 240, 255));
  ASSERT_TRUE(normal_image.save(image_path));
  TextureAsset normal;
  normal.source_path = std::filesystem::path(image_path.toStdWString());
  normal.color_space = TextureColorSpace::Linear;
  normal.usage = TextureUsage::Normal;
  MaterialDocument material;
  material.texture_slots[material_texture_slot::kNormal] =
      assets.add_texture(std::move(normal));
  filament::MaterialInstance *shaded =
      harness.materials().create_mesh_material_instance(assets, &material, false,
                                                        true);
  ASSERT_NE(shaded, nullptr);
  EXPECT_FLOAT_EQ(shaded->getParameter<float>("normalStrength"), 1.0f);
  EXPECT_FLOAT_EQ(shaded->getParameter<float>("normalYSign"), 1.0f);
  EXPECT_EQ(harness.stats().resource_count, 1u);
  EXPECT_EQ(harness.stats().material_consumer_count, 1u);
  EXPECT_EQ(harness.stats().upload_count, 1u);
  harness.materials().destroy_mesh_material_instance(harness.engine(), shaded);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().destroy_count, 1u);
}

TEST(RenderTextureLifetimeTests,
     ShadedNormalImagesShareUploadsAndRetainRetiredConsumers) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle(material_texture_slot::kNormal);
  harness.cache().sync_mesh(triangle.record);
  ASSERT_EQ(harness.stats().resource_count, 1u);
  ASSERT_EQ(harness.stats().material_consumer_count, 3u);
  ASSERT_EQ(harness.stats().upload_count, 1u);
  const auto initial_updates = harness.cache().update_stats();
  for (std::size_t frame = 0u; frame < 24u; ++frame) {
    harness.cache().sync_mesh(triangle.record);
  }
  EXPECT_EQ(harness.stats().upload_count, 1u);
  EXPECT_EQ(harness.stats().destroy_count, 0u);
  EXPECT_EQ(harness.stats().material_consumer_count, 3u);
  EXPECT_EQ(harness.cache().update_stats().full_proxy_rebuild_count,
            initial_updates.full_proxy_rebuild_count);
  EXPECT_EQ(harness.cache().update_stats().topology_stable_geometry_rebind_count,
            initial_updates.topology_stable_geometry_rebind_count);

  triangle.replace_image(2u);
  harness.cache().sync_mesh(triangle.record);
  EXPECT_EQ(harness.stats().resource_count, 2u);
  EXPECT_EQ(harness.stats().upload_count, 2u);
  harness.cache().drain_retired_destroy_queue(1u);
  EXPECT_EQ(harness.stats().resource_count, 2u)
      << "retired Shaded and depth consumers keep the old normal map alive";
  harness.drain_retired();
  EXPECT_EQ(harness.stats().resource_count, 1u);
  EXPECT_EQ(harness.stats().material_consumer_count, 3u);
  EXPECT_EQ(harness.stats().destroy_count, 1u);
  harness.cache().remove(triangle.record.entity_id);
  harness.drain_retired();
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().material_consumer_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);
}

TEST(RenderTextureLifetimeTests, ScalarUpdatesReuseTextureAcrossRetiredProxies) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle;
  harness.cache().sync_mesh(triangle.record);
  ASSERT_EQ(harness.stats().upload_count, 1u);
  for (std::size_t edit = 0u; edit < 48u; ++edit) {
    triangle.change_scalar();
    triangle.record.material.primary_material.document.diffuse_model =
        edit % 2u == 0u ? MaterialDiffuseModel::Lambert
                       : MaterialDiffuseModel::Burley;
    harness.cache().sync_mesh(triangle.record);
    harness.cache().drain_retired_destroy_queue(1u);
    EXPECT_EQ(harness.stats().resource_count, 1u);
    EXPECT_EQ(harness.stats().upload_count, 1u);
    EXPECT_EQ(harness.stats().destroy_count, 0u);
  }
  EXPECT_GT(harness.cache().retired_proxy_count(), 0u);
  harness.drain_retired();
  EXPECT_EQ(harness.stats().material_consumer_count, 3u);
  harness.cache().remove(triangle.record.entity_id);
  EXPECT_EQ(harness.stats().resource_count, 1u);
  harness.drain_retired();
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().material_consumer_count, 0u);
  EXPECT_EQ(harness.stats().destroy_count, 1u);
}

TEST(RenderTextureLifetimeTests, ReplacementAndHistoryReplayReleaseOldImages) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle;
  harness.cache().sync_mesh(triangle.record);
  // Restore older/newer immutable image revisions as undo/redo extraction does.
  for (std::uint64_t revision : {2u, 1u, 2u, 3u, 2u, 3u}) {
    triangle.replace_image(revision);
    harness.cache().sync_mesh(triangle.record);
    EXPECT_EQ(harness.stats().resource_count, 2u);
    harness.cache().drain_retired_destroy_queue(1u);
    EXPECT_EQ(harness.stats().resource_count, 2u)
        << "two detached material consumers still pin the old image";
    harness.drain_retired();
    EXPECT_EQ(harness.stats().resource_count, 1u);
    EXPECT_EQ(harness.stats().material_consumer_count, 3u);
  }
  harness.cache().shutdown();
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);
  harness.materials().bind_scene({.scene_session_id = 2u});
  harness.cache().initialize(harness.factory());
  harness.cache().sync_mesh(triangle.record);
  EXPECT_EQ(harness.stats().resource_count, 1u);
  harness.cache().shutdown();
  EXPECT_EQ(harness.stats().material_consumer_count, 0u);
}

TEST(RenderTextureLifetimeTests, PreviewDuplicatePinsImageAfterPrototypeRelease) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle;
  filament::MaterialInstance *prototype =
      harness.materials().create_mesh_material_instance(
          &triangle.record.material.primary_material, false);
  filament::MaterialInstance *duplicate =
      harness.materials().duplicate_mesh_material_instance(prototype);
  EXPECT_EQ(harness.stats().material_consumer_count, 2u);
  harness.materials().destroy_mesh_material_instance(harness.engine(), prototype);
  EXPECT_EQ(harness.stats().resource_count, 1u);
  harness.materials().bind_scene({.scene_session_id = 2u});
  EXPECT_EQ(harness.stats().resource_count, 1u)
      << "scene rebinding cannot destroy a surviving detached consumer";
  harness.materials().destroy_mesh_material_instance(harness.engine(), duplicate);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);

  // The factory path used by preview siblings must use the same tracked clone.
  MeshRenderableProxy source_proxy = harness.factory().create(
      1u, triangle.record.mesh, &triangle.record.material, QMat4{}, false, false);
  MeshRenderableProxy sibling = harness.factory().create_shared_instance(
      2u, triangle.record.mesh, source_proxy, QMat4{});
  EXPECT_EQ(harness.stats().material_consumer_count, 2u);
  harness.factory().remove_from_scene(sibling);
  EXPECT_EQ(harness.stats().material_consumer_count, 2u);
  harness.factory().destroy_detached(sibling);
  EXPECT_EQ(harness.stats().resource_count, 1u);
  harness.factory().destroy(source_proxy);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);
}

TEST(RenderTextureLifetimeTests,
     RetirementDetachesComponentsBeforeBoundedMaterialDisposal) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle;
  constexpr std::size_t kMeshCount = 96u;
  for (std::size_t index = 0u; index < kMeshCount; ++index) {
    triangle.record.entity_id = index + 1u;
    harness.cache().sync_mesh(triangle.record);
  }
  auto &renderables = harness.engine().getRenderableManager();
  ASSERT_EQ(renderables.getComponentCount(), kMeshCount * 3u);
  ASSERT_EQ(harness.stats().material_consumer_count, kMeshCount * 3u);

  harness.cache().remove_except({1u});
  constexpr std::size_t kRetiredCount = (kMeshCount - 1u) * 3u;
  EXPECT_EQ(renderables.getComponentCount(), 3u);
  EXPECT_TRUE(harness.cache().contains(1u));
  EXPECT_FALSE(harness.cache().contains(2u));
  EXPECT_EQ(harness.cache().retired_proxy_count(), kRetiredCount);
  EXPECT_EQ(harness.cache().update_stats().retired_material_instance_count,
            kRetiredCount);
  EXPECT_EQ(harness.stats().material_consumer_count, kMeshCount * 3u)
      << "detached materials must retain their texture pins until disposal";

  // Reappearance creates current components without reviving a retired proxy.
  triangle.record.entity_id = 2u;
  harness.cache().sync_mesh(triangle.record);
  EXPECT_EQ(renderables.getComponentCount(), 6u);
  EXPECT_EQ(harness.cache().retired_proxy_count(), kRetiredCount);
  harness.cache().drain_retired_destroy_queue(32u, std::chrono::hours(1));
  EXPECT_EQ(harness.cache().retired_proxy_count(), kRetiredCount - 32u);
  EXPECT_EQ(harness.cache().update_stats().retired_material_instance_count,
            kRetiredCount - 32u);
  EXPECT_EQ(harness.cache().update_stats().destroyed_retired_proxy_count, 32u);
  EXPECT_EQ(harness.stats().material_consumer_count, kRetiredCount - 32u + 6u);
  EXPECT_EQ(harness.stats().upload_count, 1u);

  harness.cache().drain_retired_destroy_queue(0u);
  harness.cache().drain_retired_destroy_queue(
      kRetiredCount, std::chrono::nanoseconds::zero());
  EXPECT_EQ(harness.cache().retired_proxy_count(), kRetiredCount - 32u);
  EXPECT_EQ(harness.cache().update_stats().destroyed_retired_proxy_count, 32u);
  harness.drain_retired();
  EXPECT_EQ(renderables.getComponentCount(), 6u);
  EXPECT_EQ(harness.stats().material_consumer_count, 6u);
  EXPECT_EQ(harness.cache().update_stats().retired_material_instance_count, 0u);
  EXPECT_EQ(harness.cache().update_stats().destroyed_retired_proxy_count,
            kRetiredCount);
  EXPECT_EQ(harness.cache().update_stats().retired_proxy_high_watermark,
            kRetiredCount);

  harness.cache().remove_except({});
  EXPECT_EQ(renderables.getComponentCount(), 0u);
  EXPECT_EQ(harness.stats().resource_count, 1u);
  harness.drain_retired();
  EXPECT_EQ(harness.stats().material_consumer_count, 0u);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.factory().geometry_cache_stats().resource_count, 1u)
      << "material retirement preserves the immutable geometry cache";
}

TEST(RenderTextureLifetimeTests, UnchangedSceneSyncDrainsRetiredMaterialBacklog) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  const auto destroy_scene = [&harness](filament::Scene *scene) {
    harness.engine().destroy(scene);
  };
  const std::unique_ptr<filament::Scene, decltype(destroy_scene)> scene(
      harness.engine().createScene(), destroy_scene);
  ASSERT_NE(scene, nullptr);
  InstanceRenderableCache instances;
  instances.initialize(harness.factory(), 64u);
  SceneRenderSyncService sync;
  sync.initialize(harness.engine(), *scene, harness.cache(), instances,
                  harness.materials());
  RenderSceneSnapshot snapshot;
  snapshot.scene_binding.scene_session_id = 1u;
  ASSERT_TRUE(sync.sync(snapshot));

  TexturedTriangle triangle;
  constexpr std::size_t kMeshCount = 96u;
  for (std::size_t index = 0u; index < kMeshCount; ++index) {
    triangle.record.entity_id = index + 1u;
    harness.cache().sync_mesh(triangle.record);
  }
  harness.cache().remove_except({});
  const auto initial_stats = harness.cache().update_stats();
  ASSERT_EQ(initial_stats.retired_proxy_count, kMeshCount * 3u);
  for (std::size_t frame = 0u;
       frame < initial_stats.retired_proxy_count &&
       harness.cache().retired_proxy_count() != 0u;
       ++frame) {
    const auto before = harness.cache().retired_proxy_count();
    EXPECT_FALSE(sync.sync(snapshot));
    const auto after = harness.cache().retired_proxy_count();
    EXPECT_LT(after, before);
    EXPECT_LE(before - after, 256u);
  }
  EXPECT_EQ(harness.cache().retired_proxy_count(), 0u);
  EXPECT_EQ(harness.cache().update_stats().retired_material_instance_count, 0u);
  EXPECT_EQ(harness.cache().update_stats().destroyed_retired_proxy_count,
            initial_stats.retired_proxy_count);
  EXPECT_EQ(harness.stats().material_consumer_count, 0u);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.cache().update_stats().full_proxy_rebuild_count,
            initial_stats.full_proxy_rebuild_count);
  EXPECT_FALSE(sync.sync(snapshot));
  EXPECT_EQ(harness.cache().update_stats().destroyed_retired_proxy_count,
            initial_stats.retired_proxy_count);
}

TEST(RenderTextureLifetimeTests,
     RetainedMeshCoverageChangesResyncAndRestoreWithoutSourceChanges) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  const auto destroy_scene = [&harness](filament::Scene *scene) {
    harness.engine().destroy(scene);
  };
  const std::unique_ptr<filament::Scene, decltype(destroy_scene)> scene(
      harness.engine().createScene(), destroy_scene);
  ASSERT_NE(scene, nullptr);
  InstanceRenderableCache instances;
  instances.initialize(harness.factory(), 64u);
  SceneRenderSyncService sync;
  sync.initialize(harness.engine(), *scene, harness.cache(), instances,
                  harness.materials());
  TexturedTriangle triangle;
  RenderSceneSnapshot snapshot;
  snapshot.scene_binding.scene_session_id = 1u;
  snapshot.meshes = {triangle.record, triangle.record};
  snapshot.meshes.back().entity_id = 2u;
  snapshot.live_mesh_entities = {1u, 2u};
  RenderSceneLightRecord light;
  light.entity_id = 20u;
  light.light.type = LightType::Point;
  light.light.intensity = 2.0f;
  light.light.range = 2.0f;
  light.rebuild_revision = LightRenderableCache::light_signature(light.light);
  snapshot.lights.push_back(light);
  snapshot.live_light_entities.insert(light.entity_id);
  ASSERT_TRUE(sync.sync(snapshot));
  ASSERT_TRUE(harness.cache().contains(1u));
  ASSERT_TRUE(harness.cache().contains(2u));
  const RenderSourceStamp original_stamp = snapshot;

  RenderToolPreviewSuppressionSnapshot exclusion;
  exclusion.signature = 1u;
  exclusion.entities = {1u};
  ASSERT_TRUE(sync.set_retained_mesh_exclusion(exclusion));
  ASSERT_TRUE(sync.sync(snapshot));
  EXPECT_FALSE(harness.cache().contains(1u));
  EXPECT_TRUE(harness.cache().contains(2u));
  EXPECT_EQ(harness.engine().getRenderableManager().getComponentCount(), 3u);
  harness.drain_retired();
  const auto retained_stats = harness.cache().update_stats();
  const auto geometry_uploads = harness.factory().geometry_cache_stats().upload_count;
  const auto texture_uploads = harness.stats().upload_count;
  for (std::size_t frame = 0u; frame < 3u; ++frame) {
    EXPECT_FALSE(sync.set_retained_mesh_exclusion(exclusion));
    EXPECT_FALSE(sync.sync(snapshot));
  }
  EXPECT_EQ(harness.cache().update_stats().full_proxy_rebuild_count,
            retained_stats.full_proxy_rebuild_count);
  EXPECT_EQ(harness.cache().update_stats().destroyed_retired_proxy_count,
            retained_stats.destroyed_retired_proxy_count);
  EXPECT_EQ(harness.factory().geometry_cache_stats().upload_count, geometry_uploads);
  EXPECT_EQ(harness.stats().upload_count, texture_uploads);

  // Exact coverage must win even when its signature and source stamps match.
  exclusion.entities = {2u};
  ASSERT_TRUE(sync.set_retained_mesh_exclusion(exclusion));
  ASSERT_TRUE(sync.sync(snapshot));
  EXPECT_TRUE(harness.cache().contains(1u));
  EXPECT_FALSE(harness.cache().contains(2u));
  EXPECT_EQ(static_cast<const RenderSourceStamp &>(snapshot), original_stamp);

  auto &lights = harness.engine().getLightManager();
  ASSERT_EQ(lights.getComponentCount(), 1u);
  const auto light_entity = lights.getEntities()[0u];
  const float original_intensity = lights.getIntensity(lights.getInstance(light_entity));
  snapshot.lights.front().light.intensity *= 2.0f;
  snapshot.lights.front().rebuild_revision =
      LightRenderableCache::light_signature(snapshot.lights.front().light);
  ++snapshot.light_property_revision;
  ++snapshot.scene_revision;
  ASSERT_TRUE(sync.sync(snapshot));
  ASSERT_TRUE(lights.hasComponent(light_entity));
  EXPECT_FLOAT_EQ(lights.getIntensity(lights.getInstance(light_entity)),
                  original_intensity * 2.0f);
  EXPECT_TRUE(harness.cache().contains(1u));
  EXPECT_FALSE(harness.cache().contains(2u));

  const RenderSourceStamp restore_stamp = snapshot;
  ASSERT_TRUE(sync.set_retained_mesh_exclusion({}));
  ASSERT_TRUE(sync.sync(snapshot));
  EXPECT_TRUE(harness.cache().contains(1u));
  EXPECT_TRUE(harness.cache().contains(2u));
  EXPECT_EQ(harness.engine().getRenderableManager().getComponentCount(), 6u);
  EXPECT_EQ(static_cast<const RenderSourceStamp &>(snapshot), restore_stamp);
  EXPECT_FALSE(sync.set_retained_mesh_exclusion({}));
  EXPECT_FALSE(sync.sync(snapshot));
}

TEST(RenderTextureLifetimeTests,
     InstancePreviewSuppressionCannotRestoreExternallyCoveredOccurrences) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  const auto destroy_scene = [&harness](filament::Scene *scene) {
    harness.engine().destroy(scene);
  };
  const std::unique_ptr<filament::Scene, decltype(destroy_scene)> scene(
      harness.engine().createScene(), destroy_scene);
  ASSERT_NE(scene, nullptr);
  InstanceRenderableCache instances;
  instances.initialize(harness.factory(), 64u);
  SceneRenderSyncService sync;
  sync.initialize(harness.engine(), *scene, harness.cache(), instances,
                  harness.materials());
  TexturedTriangle triangle;
  RenderSceneSnapshot snapshot;
  snapshot.scene_binding.scene_session_id = 1u;
  for (EntityId member = 11u; member <= 12u; ++member) {
    RenderSceneInstancePrimitiveRecord primitive;
    primitive.definition_entity = 10u;
    primitive.member_entity = member;
    primitive.key.geometry_payload = member;
    primitive.key.index_count = 3u;
    primitive.mesh = triangle.record.mesh;
    primitive.primitive.index_count = 3u;
    primitive.material = triangle.record.material;
    primitive.local_bounds = MeshRenderableFactory::compute_local_bounds(primitive.mesh);
    for (EntityId entity = 101u; entity <= 104u; ++entity) {
      primitive.occurrences.push_back({.entity_id = entity,
                                       .occurrence = member * 100u + entity});
    }
    snapshot.instance_primitives.push_back(std::move(primitive));
  }
  ASSERT_TRUE(sync.sync(snapshot));
  ASSERT_EQ(instances.stats().participant_count, 8u);
  const RenderSourceStamp original_stamp = snapshot;
  RenderToolPreviewSuppressionSnapshot exclusion;
  exclusion.signature = 1u;
  exclusion.entities = {104u};
  exclusion.occurrences = {{.source_entity = 11u, .occurrence = 101u},
                           {.source_entity = 10u, .occurrence = 103u},
                           {.source_entity = 999u, .occurrence = 102u}};
  ASSERT_TRUE(sync.set_retained_mesh_exclusion(exclusion));
  ASSERT_TRUE(sync.sync(snapshot));
  ASSERT_EQ(instances.stats().participant_count, 3u);
  const auto ordinary = sync.ordinary_instance_primitives(snapshot);
  ASSERT_EQ(ordinary.size(), 2u);
  ASSERT_EQ(ordinary[0u].occurrences.size(), 1u);
  EXPECT_EQ(ordinary[0u].member_entity, 11u);
  EXPECT_EQ(ordinary[0u].occurrences[0u].entity_id, 102u);
  ASSERT_EQ(ordinary[1u].occurrences.size(), 2u);
  EXPECT_EQ(ordinary[1u].member_entity, 12u);
  EXPECT_EQ(ordinary[1u].occurrences[0u].entity_id, 101u);
  EXPECT_EQ(ordinary[1u].occurrences[1u].entity_id, 102u);
  EXPECT_EQ(snapshot.instance_primitives[0u].occurrences.size(), 4u);
  EXPECT_EQ(snapshot.instance_primitives[1u].occurrences.size(), 4u);
  const auto retained_stats = instances.stats();
  EXPECT_FALSE(sync.set_retained_mesh_exclusion(exclusion));
  EXPECT_FALSE(sync.sync(snapshot));
  EXPECT_EQ(instances.stats().rebuild_count, retained_stats.rebuild_count);
  EXPECT_EQ(instances.stats().transform_update_count,
            retained_stats.transform_update_count);
  EXPECT_EQ(instances.stats().proxy_creation_count,
            retained_stats.proxy_creation_count);

  instances.set_preview_suppressed_entities(
      {102u}, {}, sync.ordinary_instance_primitives(snapshot));
  ASSERT_TRUE(instances.last_sync_succeeded());
  EXPECT_EQ(instances.stats().participant_count, 1u);
  instances.set_preview_suppressed_entities(
      {102u}, {{.source_entity = 10u, .occurrence = 101u}},
      sync.ordinary_instance_primitives(snapshot));
  ASSERT_TRUE(instances.last_sync_succeeded());
  EXPECT_EQ(instances.stats().participant_count, 0u);
  instances.set_preview_suppressed_entities(
      {}, {}, sync.ordinary_instance_primitives(snapshot));
  ASSERT_TRUE(instances.last_sync_succeeded());
  EXPECT_EQ(instances.stats().participant_count, 3u);
  EXPECT_FALSE(sync.sync(snapshot));
  EXPECT_EQ(instances.stats().participant_count, 3u);

  // Excluding the last occurrence of a member drops that primitive entirely.
  exclusion.occurrences.push_back({.source_entity = 11u, .occurrence = 102u});
  ASSERT_TRUE(sync.set_retained_mesh_exclusion(exclusion));
  ASSERT_TRUE(sync.sync(snapshot));
  ASSERT_EQ(sync.ordinary_instance_primitives(snapshot).size(), 1u);
  EXPECT_EQ(sync.ordinary_instance_primitives(snapshot).front().member_entity, 12u);
  EXPECT_EQ(instances.stats().participant_count, 2u);
  ASSERT_TRUE(sync.set_retained_mesh_exclusion({}));
  ASSERT_TRUE(sync.sync(snapshot));
  EXPECT_EQ(instances.stats().participant_count, 8u);
  ASSERT_EQ(sync.ordinary_instance_primitives(snapshot).size(), 2u);
  EXPECT_EQ(sync.ordinary_instance_primitives(snapshot)[0u].occurrences.size(), 4u);
  EXPECT_EQ(sync.ordinary_instance_primitives(snapshot)[1u].occurrences.size(), 4u);
  EXPECT_EQ(static_cast<const RenderSourceStamp &>(snapshot), original_stamp);
  EXPECT_FALSE(sync.sync(snapshot));
}

TEST(RenderTextureLifetimeTests, VulkanMaterialEditAndRetirementStress) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_TEXTURE_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_TEXTURE_TESTS=1 for offscreen GPU "
                    "texture retirement verification.";
  }
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN));
  TexturedTriangle triangle;
  harness.cache().sync_mesh(triangle.record);
  ASSERT_TRUE(harness.frame());
  for (std::uint64_t edit = 1u; edit <= 48u; ++edit) {
    triangle.change_scalar();
    harness.cache().sync_mesh(triangle.record);
    ASSERT_TRUE(harness.frame());
    harness.drain_retired();
    EXPECT_EQ(harness.stats().upload_count, 1u);
    EXPECT_EQ(harness.stats().resource_count, 1u);
  }
  for (std::uint64_t revision : {2u, 3u, 2u, 1u, 2u, 3u, 4u, 3u, 2u, 1u}) {
    triangle.replace_image(revision);
    harness.cache().sync_mesh(triangle.record);
    ASSERT_TRUE(harness.frame());
    EXPECT_EQ(harness.stats().resource_count, 2u);
    harness.cache().drain_retired_destroy_queue(1u);
    ASSERT_TRUE(harness.frame());
    EXPECT_EQ(harness.stats().resource_count, 2u);
    harness.drain_retired();
    ASSERT_TRUE(harness.frame());
    EXPECT_EQ(harness.stats().resource_count, 1u);
  }
  harness.cache().shutdown();
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);
  harness.materials().bind_scene({.scene_session_id = 2u});
  harness.cache().initialize(harness.factory());
  harness.cache().sync_mesh(triangle.record);
  ASSERT_TRUE(harness.frame());
  harness.cache().shutdown();
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().resource_count, 0u);
}

TEST(RenderTextureLifetimeTests, InstanceRetirementPreservesRetainedChunks) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle;
  std::vector<RenderSceneInstancePrimitiveRecord> records;
  constexpr std::size_t kMeshCount = 256u;
  for (std::size_t index = 0u; index < kMeshCount; ++index) {
    RenderSceneInstancePrimitiveRecord record;
    record.key.geometry_payload = index + 1u;
    record.key.index_count = 3u;
    record.mesh = triangle.record.mesh;
    record.primitive.index_count = 3u;
    record.material = triangle.record.material;
    record.local_bounds = MeshRenderableFactory::compute_local_bounds(record.mesh);
    record.occurrences.push_back({.entity_id = index + 1u,
                                  .occurrence = index + 1u});
    records.push_back(std::move(record));
  }
  InstanceRenderableCache instances;
  instances.initialize(harness.factory(), 1u);
  ASSERT_TRUE(instances.sync(records));
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(instances.stats().chunk_count, kMeshCount * 3u);
  EXPECT_EQ(harness.stats().material_consumer_count, kMeshCount * 3u);
  const auto geometry_uploads = harness.factory().geometry_cache_stats().upload_count;

  records.resize(kMeshCount / 2u);
  std::vector<MeshVertex> deformed(triangle.record.mesh.vertices.begin(),
                                   triangle.record.mesh.vertices.end());
  for (MeshVertex &vertex : deformed) vertex.position.x += 0.25f;
  for (auto &record : records) {
    record.key.geometry_payload += kMeshCount;
    ++record.mesh.mesh_revision;
    record.mesh.vertices = deformed;
    record.local_bounds = MeshRenderableFactory::compute_local_bounds(record.mesh);
  }
  std::ranges::reverse(records);
  ASSERT_TRUE(instances.sync(records));
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(instances.stats().chunk_count, records.size() * 3u);
  EXPECT_EQ(harness.stats().material_consumer_count, records.size() * 3u);
  EXPECT_EQ(harness.factory().geometry_cache_stats().upload_count, geometry_uploads + 1u);
  EXPECT_EQ(instances.stats().geometry_rebind_count, records.size() * 3u);
  EXPECT_EQ(instances.stats().proxy_creation_count, kMeshCount * 3u);
  EXPECT_EQ(instances.stats().proxy_retirement_count, records.size() * 3u);
  EXPECT_FALSE(instances.sync(records));
  EXPECT_EQ(harness.stats().material_consumer_count, records.size() * 3u);
  EXPECT_EQ(instances.stats().geometry_rebind_count, records.size() * 3u);

  // Material changes still rebuild their owning passes, while unsupported
  // topology changes take the same fallback without touching other chunks.
  ++records.front().key.material_payload;
  records.front().material.primary_material.document.roughness = 0.93f;
  ASSERT_TRUE(instances.sync(records));
  EXPECT_EQ(instances.stats().proxy_creation_count, kMeshCount * 3u + 3u);
  EXPECT_EQ(instances.stats().proxy_retirement_count, records.size() * 3u + 3u);
  EXPECT_EQ(instances.stats().geometry_rebind_count, records.size() * 3u);
  const std::array<std::uint32_t, 3u> reversed_indices{0u, 2u, 1u};
  records.front().mesh.indices = reversed_indices;
  records.front().key.geometry_payload += kMeshCount;
  ASSERT_TRUE(instances.sync(records));
  EXPECT_EQ(instances.stats().proxy_creation_count, kMeshCount * 3u + 6u);
  EXPECT_EQ(instances.stats().proxy_retirement_count, records.size() * 3u + 6u);
  EXPECT_EQ(instances.stats().geometry_rebind_count, records.size() * 3u);
  instances.shutdown();
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.engine().getRenderableManager().getComponentCount(), 0u);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);
}

TEST(RenderTextureLifetimeTests, InstanceGeometryRebindRetainsGpuOwnersAndRejectsWholeInvalidBatch) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle;
  const std::array<QMat4, 1u> transforms{QMat4{}};
  const MeshPrimitiveSpan primitive{.index_count = 3u};
  const auto bounds = MeshRenderableFactory::compute_local_bounds(triangle.record.mesh);
  std::vector<MeshRenderableProxy> proxies;
  for (std::size_t index = 0u; index < 3u; ++index) {
    proxies.push_back(harness.factory().create_explicit_instances(
        triangle.record.mesh, &triangle.record.material, primitive, transforms,
        bounds, false, false, false, kViewportRenderedSceneLayerMask));
    ASSERT_TRUE(proxies.back().entity);
  }
  std::vector<MeshVertex> deformed(triangle.record.mesh.vertices.begin(),
                                   triangle.record.mesh.vertices.end());
  deformed.front().position.x -= 0.25f;
  auto replacement_mesh = triangle.record.mesh;
  replacement_mesh.vertices = deformed;
  ++replacement_mesh.mesh_revision;
  std::vector<MeshRenderableFactory::InstanceGeometryRebind> requests;
  std::vector<utils::Entity> entities;
  std::vector<filament::InstanceBuffer *> instance_buffers;
  std::vector<filament::MaterialInstance *> materials;
  const auto *previous_vertex_buffer = proxies.front().vertex_buffer;
  for (auto &proxy : proxies) {
    requests.push_back({.proxy = &proxy, .mesh = &replacement_mesh,
                        .primitive = primitive, .world_bounds = bounds});
    entities.push_back(proxy.entity);
    instance_buffers.push_back(proxy.instance_buffer);
    materials.push_back(proxy.material_instances.front());
  }
  ASSERT_TRUE(harness.factory().try_rebind_instance_geometry(requests));
  EXPECT_NE(proxies.front().vertex_buffer, previous_vertex_buffer);
  for (std::size_t index = 0u; index < proxies.size(); ++index) {
    EXPECT_EQ(proxies[index].entity, entities[index]);
    EXPECT_EQ(proxies[index].instance_buffer, instance_buffers[index]);
    EXPECT_EQ(proxies[index].material_instances.front(), materials[index]);
    EXPECT_EQ(proxies[index].mesh_revision, replacement_mesh.mesh_revision);
  }
  ASSERT_TRUE(harness.frame());

  const auto *rebound_vertex_buffer = proxies.front().vertex_buffer;
  const auto upload_count = harness.factory().geometry_cache_stats().upload_count;
  const std::array<std::uint32_t, 3u> reversed_indices{0u, 2u, 1u};
  auto invalid_mesh = replacement_mesh;
  invalid_mesh.indices = reversed_indices;
  requests.back().mesh = &invalid_mesh;
  EXPECT_FALSE(harness.factory().try_rebind_instance_geometry(requests));
  EXPECT_EQ(harness.factory().geometry_cache_stats().upload_count, upload_count);
  for (const auto &proxy : proxies) EXPECT_EQ(proxy.vertex_buffer, rebound_vertex_buffer);
  harness.factory().destroy_renderable_components(entities);
  for (auto &proxy : proxies) harness.factory().destroy_detached(proxy);
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().material_consumer_count, 0u);
}

TEST(RenderTextureLifetimeTests, VulkanLargeSceneShutdownRetiresMeshResources) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_TEXTURE_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_TEXTURE_TESTS=1 for large GPU teardown.";
  }
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN, 32u));
  EXPECT_TRUE(harness.engine().getFeatureFlag("engine.debug.assert_material_instance_in_use")
                  .value_or(false));
  TexturedTriangle triangle;
  std::vector<MeshVertex> vertices(triangle.record.mesh.vertices.begin(),
                                    triangle.record.mesh.vertices.end());
  triangle.record.mesh.vertices = vertices;
  constexpr std::size_t kMeshCount = 6400u;
  for (std::size_t index = 0u; index < kMeshCount; ++index) {
    triangle.record.entity_id = index + 1u;
    triangle.record.mesh.mesh_id = index + 1u;
    for (MeshVertex &vertex : vertices) {
      vertex.position.z = static_cast<float>(index) * 0.0001f;
    }
    harness.cache().sync_mesh(triangle.record);
    if (index % 32u == 31u) {
      harness.engine().flush();
    }
  }
  ASSERT_TRUE(harness.frame());
  ASSERT_EQ(harness.engine().getRenderableManager().getComponentCount(), kMeshCount * 3u);
  ASSERT_EQ(harness.factory().geometry_cache_stats().resource_count, kMeshCount);

  std::unordered_set<EntityId> retained_entities;
  for (std::size_t index = 0u; index < kMeshCount / 2u; ++index) {
    retained_entities.insert(index + 1u);
  }
  harness.cache().remove_except(retained_entities);
  EXPECT_EQ(harness.engine().getRenderableManager().getComponentCount(),
            kMeshCount / 2u * 3u);
  ASSERT_EQ(harness.cache().retired_proxy_count(), kMeshCount / 2u * 3u);
  EXPECT_EQ(harness.stats().material_consumer_count, kMeshCount * 3u);
  harness.cache().drain_retired_destroy_queue(128u, std::chrono::milliseconds(2));
  const auto retired_stats = harness.cache().update_stats();
  EXPECT_GT(retired_stats.destroyed_retired_proxy_count, 0u);
  EXPECT_LE(retired_stats.destroyed_retired_proxy_count, 128u);
  EXPECT_EQ(retired_stats.retired_material_instance_count,
            retired_stats.retired_proxy_count);
  ASSERT_TRUE(harness.frame());

  const auto retirement_start = std::chrono::steady_clock::now();
  harness.cache().shutdown();
  const auto proxy_retired = std::chrono::steady_clock::now();
  harness.factory().shutdown();
  ASSERT_TRUE(harness.engine().flushAndWait(10'000'000'000ull));
  harness.engine().pumpMessageQueues();
  const auto completed = std::chrono::steady_clock::now();
  EXPECT_EQ(harness.engine().getRenderableManager().getComponentCount(), 0u);
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);
  EXPECT_EQ(harness.factory().geometry_cache_stats().resource_count, 0u);
  // Teardown remains idempotent when owner destructors run after explicit close.
  harness.cache().shutdown();
  harness.factory().shutdown();
  std::cout << "Large scene GPU shutdown: meshes=" << kMeshCount
            << " proxies=" << kMeshCount * 3u << " proxy_retire_ms="
            << std::chrono::duration<double, std::milli>(proxy_retired - retirement_start).count()
            << " geometry_and_drain_ms="
            << std::chrono::duration<double, std::milli>(completed - proxy_retired).count()
            << std::endl;
}

TEST(RenderTextureLifetimeTests, VulkanLargeWireRetirementReleasesPrunedAndLiveResources) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_TEXTURE_TESTS") != QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_TEXTURE_TESTS=1 for large wire teardown.";
  }
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN, 32u));
  FilamentMaterialLibrary library;
  ASSERT_TRUE(library.initialize(harness.engine(),
      std::filesystem::path(qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString())));
  auto *wire_scene = harness.engine().createScene();
  InstancedWireRenderBackend wires;
  wires.initialize(harness.engine(), *wire_scene, library.selected_object_line(), 1u);
  const std::array<MeshWireSegment, 1u> segments{{
      {.start = {-1.0f, 0.0f, 0.0f}, .end = {1.0f, 0.0f, 0.0f}}}};
  RenderInstanceBatchChunk chunk;
  chunk.world_transforms.emplace_back();
  InstancedWireBatchStyle style;
  style.layer_mask = kViewportOverlayLayerMask;
  std::vector<InstancedWireRenderBackend::BatchProxy> proxies;
  constexpr std::size_t kWireCount = 6400u;
  proxies.reserve(kWireCount);
  for (std::size_t index = 0u; index < kWireCount; ++index) {
    auto proxy = wires.create_batch({.signature = index + 1u,
                                     .color = 0xffffffffu,
                                     .segments = segments}, chunk, style);
    ASSERT_TRUE(wires.valid(proxy));
    proxies.push_back(std::move(proxy));
    if (index % 32u == 31u) {
      harness.engine().flush();
    }
  }
  ASSERT_TRUE(harness.engine().flushAndWait(10'000'000'000ull));
  const auto retirement_start = std::chrono::steady_clock::now();
  wires.destroy_batches(std::span(proxies).first(kWireCount / 2u));
  wires.prune_unused_geometry_resources(std::span(proxies).subspan(kWireCount / 2u));
  EXPECT_EQ(wires.stats().unique_geometry_resource_count, kWireCount / 2u);
  for (const auto &proxy : std::span(proxies).subspan(kWireCount / 2u)) {
    EXPECT_TRUE(wires.valid(proxy));
  }
  wires.destroy_batches(proxies);
  wires.shutdown();
  harness.engine().destroy(wire_scene);
  library.shutdown();
  ASSERT_TRUE(harness.engine().flushAndWait(10'000'000'000ull));
  EXPECT_EQ(harness.engine().getRenderableManager().getComponentCount(), 0u);
  std::cout << "Large wire GPU retirement: batches=" << kWireCount << " retire_ms="
            << std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - retirement_start).count()
            << std::endl;
}

TEST(RenderTextureLifetimeTests, VulkanSingleEdgeBevelSettingsUpdateScenePixels) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_TEXTURE_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_TEXTURE_TESTS=1 for windowless Bevel "
                    "preview pixel verification.";
  }
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN, 256u));
  harness.initialize_scene_bridge();
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  const auto source = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Bevel GPU settings source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(source.value(), 1, 2).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Move).ok());

  const auto capture = [&] {
    const auto scene = QuaderRenderApiRuntimeAccess::sync_scene(
        editor.render(), harness.bridge());
    EXPECT_TRUE(scene.ok()) << scene.error_message();
    const auto preview = QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), harness.bridge());
    EXPECT_TRUE(preview.ok()) << preview.error_message();
    // Opposite views expose every box edge without depending on the primitive's
    // particular vertex ordering. Post-processing is disabled by the harness.
    std::array<QImage, 2u> images{
        harness.capture_scene(1), harness.capture_scene(-1)};
    for (const QImage &image : images) EXPECT_FALSE(image.isNull());
    return images;
  };
  const auto changed_pixels = [](const std::array<QImage, 2u> &before,
                                const std::array<QImage, 2u> &after) {
    std::size_t count = 0u;
    for (std::size_t view = 0; view < before.size(); ++view) {
      if (before[view].isNull() || after[view].isNull() ||
          before[view].size() != after[view].size()) continue;
      for (int y = 0; y < before[view].height(); ++y) {
        for (int x = 0; x < before[view].width(); ++x) {
          const QRgb previous = before[view].pixel(x, y);
          const QRgb current = after[view].pixel(x, y);
          if (std::abs(qRed(previous) - qRed(current)) > 2 ||
              std::abs(qGreen(previous) - qGreen(current)) > 2 ||
              std::abs(qBlue(previous) - qBlue(current)) > 2) ++count;
        }
      }
    }
    return count;
  };

  const auto original = capture();
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.15).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.segments", 1.0).ok());
  const auto narrow = capture();
  const auto narrow_resources = harness.bridge().diagnostics();
  EXPECT_GT(changed_pixels(original, narrow), 8u);

  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.7).ok());
  const auto wide = capture();
  const auto wide_resources = harness.bridge().diagnostics();
  EXPECT_GT(wide_resources.gpu_geometry_upload_count,
            narrow_resources.gpu_geometry_upload_count);
  EXPECT_GT(changed_pixels(narrow, wide), 8u)
      << "Width-only preview changes must reach the retained GPU geometry.";

  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.segments", 5.0).ok());
  const auto segmented = capture();
  const auto segmented_resources = harness.bridge().diagnostics();
  EXPECT_GT(segmented_resources.gpu_geometry_upload_count,
            wide_resources.gpu_geometry_upload_count);
  EXPECT_GT(changed_pixels(wide, segmented), 8u);

  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.profile", 0.2).ok());
  const auto profiled = capture();
  const auto profiled_resources = harness.bridge().diagnostics();
  EXPECT_GT(profiled_resources.gpu_geometry_upload_count,
            segmented_resources.gpu_geometry_upload_count);
  EXPECT_GT(changed_pixels(segmented, profiled), 8u);

  const auto unchanged = capture();
  EXPECT_EQ(unchanged, profiled);
  EXPECT_EQ(harness.bridge().diagnostics().gpu_geometry_upload_count,
            profiled_resources.gpu_geometry_upload_count);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  const auto canceled = capture();
  EXPECT_EQ(canceled, original);
}

} // namespace
} // namespace quader

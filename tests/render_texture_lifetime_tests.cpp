////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_material_library.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/texture_binding_service.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace quader {
namespace {

/// Exercises actual resource owners using the no-op driver or opt-in Vulkan.
class TextureLifetimeHarness final {
public:
  ~TextureLifetimeHarness() {
    if (engine_ == nullptr) {
      return;
    }
    cache_.shutdown();
    factory_.shutdown();
    materials_.shutdown();
    textures_.shutdown();
    library_.shutdown();
    if (view_ != nullptr) {
      engine_->destroy(view_);
    }
    if (camera_entity_) {
      engine_->destroyCameraComponent(camera_entity_);
      utils::EntityManager::get().destroy(camera_entity_);
    }
    if (renderer_ != nullptr) {
      engine_->destroy(renderer_);
    }
    if (swap_chain_ != nullptr) {
      engine_->destroy(swap_chain_);
    }
    if (scene_ != nullptr) {
      engine_->destroy(scene_);
    }
    filament::Engine::destroy(&engine_);
  }

  bool initialize(filament::Engine::Backend backend =
                      filament::Engine::Backend::NOOP) {
    engine_ = filament::Engine::create(backend);
    if (engine_ == nullptr) {
      return false;
    }
    const std::filesystem::path resource_root(
        qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
    if (!library_.initialize(*engine_, resource_root)) {
      return false;
    }
    scene_ = engine_->createScene();
    textures_.initialize(*engine_, resource_root);
    materials_.initialize(library_.quader_mesh(), library_.quader_mesh_lit(),
                          textures_);
    materials_.bind_scene({.scene_session_id = 1u});
    factory_.initialize(*engine_, *scene_, materials_);
    cache_.initialize(factory_);
    renderer_ = engine_->createRenderer();
    swap_chain_ = engine_->createSwapChain(128u, 128u);
    view_ = engine_->createView();
    camera_entity_ = utils::EntityManager::get().create();
    filament::Camera *camera = engine_->createCamera(camera_entity_);
    camera->setProjection(45.0, 1.0, 0.1, 20.0);
    camera->lookAt({0.0, 0.0, 4.0}, {0.0, 0.0, 0.0});
    view_->setCamera(camera);
    view_->setScene(scene_);
    view_->setViewport({0u, 0u, 128u, 128u});
    view_->setVisibleLayers(0xffu, kViewportRenderedSceneLayerMask);
    view_->setPostProcessingEnabled(false);
    return renderer_ != nullptr && swap_chain_ != nullptr;
  }

  bool frame() {
    // A renderer can reject an early frame while prior GPU work is in flight.
    for (std::size_t attempt = 0u; attempt < 32u; ++attempt) {
      if (renderer_->beginFrame(swap_chain_)) {
        renderer_->render(view_);
        renderer_->endFrame();
        return engine_->flushAndWait(10'000'000'000ull);
      }
      if (!engine_->flushAndWait(10'000'000'000ull)) {
        return false;
      }
    }
    return false;
  }

  void drain_retired() {
    cache_.drain_retired_destroy_queue(cache_.retired_proxy_count());
  }
  TextureBindingService::CacheStats stats() const {
    return textures_.cache_stats();
  }
  filament::Engine &engine() { return *engine_; }
  MeshRenderableCache &cache() { return cache_; }
  MeshRenderableFactory &factory() { return factory_; }
  MaterialBindingService &materials() { return materials_; }
  TextureBindingService &textures() { return textures_; }

private:
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swap_chain_ = nullptr;
  filament::View *view_ = nullptr;
  utils::Entity camera_entity_;
  FilamentMaterialLibrary library_;
  TextureBindingService textures_;
  MaterialBindingService materials_;
  MeshRenderableFactory factory_;
  MeshRenderableCache cache_;
};

/// Owns immutable image and geometry inputs for render-cache lifecycle tests.
class TexturedTriangle final {
public:
  TexturedTriangle() {
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
    texture.slot = material_texture_slot::kBaseColor;
    texture.project_texture_id = *ProjectAssetId::parse(
        "f7097612-7f3b-481b-98cb-5dce4a983aec");
    texture.texture.color_space = TextureColorSpace::Srgb;
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

TEST(RenderTextureLifetimeTests, ScalarUpdatesReuseTextureAcrossRetiredProxies) {
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  TexturedTriangle triangle;
  harness.cache().sync_mesh(triangle.record);
  ASSERT_EQ(harness.stats().upload_count, 1u);
  for (std::size_t edit = 0u; edit < 48u; ++edit) {
    triangle.change_scalar();
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

} // namespace
} // namespace quader

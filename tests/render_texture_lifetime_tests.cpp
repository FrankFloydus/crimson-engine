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
#include "render/mesh_renderable_cache.hpp"
#include "render/png_texture_loader.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/texture_binding_service.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QTemporaryDir>

#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <backend/PixelBufferDescriptor.h>
#include <utils/EntityManager.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
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
    (void)engine_->flushAndWait(10'000'000'000ull);
    engine_->pumpMessageQueues();
    bridge_.shutdown();
    cache_.shutdown();
    factory_.shutdown();
    materials_.shutdown();
    textures_.shutdown();
    library_.shutdown();
    if (solid_color_texture_ != nullptr) {
      engine_->destroy(solid_color_texture_);
      solid_color_texture_ = nullptr;
    }
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
                      filament::Engine::Backend::NOOP,
                  std::uint32_t size = 128u) {
    size_ = size;
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
    const std::array<std::byte, 4u> neutral_gray{
        std::byte{181u}, std::byte{181u}, std::byte{181u}, std::byte{255u}};
    solid_color_texture_ = load_rgba_texture(
        *engine_, neutral_gray, 1u, 1u,
        PngTextureLoadOptions{
            .color_space = PngTextureColorSpace::Srgb,
            .generate_mipmaps = true,
            .label = "test Solid viewport texture",
        });
    if (solid_color_texture_ == nullptr) {
      return false;
    }
    textures_.initialize(*engine_, resource_root, *solid_color_texture_);
    materials_.initialize(library_.quader_mesh(), library_.quader_mesh_lit(),
                          textures_);
    materials_.bind_scene({.scene_session_id = 1u});
    factory_.initialize(*engine_, *scene_, materials_);
    cache_.initialize(factory_);
    renderer_ = engine_->createRenderer();
    swap_chain_ = engine_->createSwapChain(
        size_, size_, filament::SwapChain::CONFIG_READABLE);
    view_ = engine_->createView();
    camera_entity_ = utils::EntityManager::get().create();
    filament::Camera *camera = engine_->createCamera(camera_entity_);
    camera->setProjection(45.0, 1.0, 0.1, 20.0);
    camera->lookAt({0.0, 0.0, 4.0}, {0.0, 0.0, 0.0});
    view_->setCamera(camera);
    view_->setScene(scene_);
    view_->setViewport({0u, 0u, size_, size_});
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

  void initialize_scene_bridge() {
    const std::filesystem::path resources(
        qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
    bridge_.initialize(
        *engine_, *scene_, library_.quader_mesh(), library_.quader_mesh_lit(),
        library_.vertex_color_overlay(), library_.instance_selection_fill(),
        library_.overlay_line(), library_.selected_object_line(),
        library_.edit_wire_line(), library_.overlay_point(),
        library_.overlay_icon(), *solid_color_texture_, resources);
    view_->setVisibleLayers(0xffu, kViewportShadedSceneLayerMask);
    renderer_->setClearOptions(
        {.clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, .clear = true});
  }

  SceneRenderBridge &bridge() { return bridge_; }

  QImage capture_scene(int corner) {
    auto *camera = engine_->getCameraComponent(camera_entity_);
    const double coordinate = 4.0 * static_cast<double>(corner);
    camera->lookAt({coordinate, coordinate, coordinate}, {0.0, 0.0, 0.0});
    for (int frame_index = 0; frame_index < 3; ++frame_index) {
      if (!frame()) return {};
    }
    bool begun = false;
    for (std::size_t attempt = 0; attempt < 32u && !begun; ++attempt) {
      begun = renderer_->beginFrame(swap_chain_);
      if (!begun && !engine_->flushAndWait(10'000'000'000ull)) return {};
    }
    if (!begun) return {};
    renderer_->render(view_);
    auto readback = std::make_shared<Readback>();
    readback->pixels.resize(size_ * size_ * 4u);
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        0u, 0u, size_, size_,
        filament::backend::PixelBufferDescriptor(
            readback->pixels.data(), readback->pixels.size(),
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            [](void *, std::size_t, void *user) {
              auto *holder = static_cast<std::shared_ptr<Readback> *>(user);
              (*holder)->complete.store(true, std::memory_order_release);
              delete holder;
            }, owner));
    renderer_->endFrame();
    for (int attempt = 0; attempt < 10 &&
         !readback->complete.load(std::memory_order_acquire); ++attempt) {
      if (!engine_->flushAndWait(1'000'000'000ull)) return {};
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) return {};
    return QImage(readback->pixels.data(), static_cast<int>(size_),
                  static_cast<int>(size_), QImage::Format_RGBA8888).copy();
  }

private:
  /// Owns asynchronous pixel bytes until Filament acknowledges the transfer.
  struct Readback {
    std::vector<unsigned char> pixels;
    std::atomic<bool> complete{false};
  };

  std::uint32_t size_ = 128u;
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
  SceneRenderBridge bridge_;
  filament::Texture *solid_color_texture_ = nullptr;
};

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

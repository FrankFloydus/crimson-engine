////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/filament_material_library.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/png_texture_loader.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/texture_binding_service.hpp"

#include <QImage>
#include <QString>

#include <backend/PixelBufferDescriptor.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace quader::test {

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

} // namespace quader::test

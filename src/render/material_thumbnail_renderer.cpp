////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/material_thumbnail_renderer.hpp"

#include "render/material_preview_renderer.hpp"

#include <backend/PixelBufferDescriptor.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>

#include <utility>

namespace quader {

/// Retains only render resources; asynchronous callbacks never touch this owner.
struct MaterialThumbnailRenderer::Impl final {
  ~Impl() { shutdown(); }

  void destroy_target() {
    if (target != nullptr) {
      engine->destroy(target);
      target = nullptr;
    }
    if (depth != nullptr) {
      engine->destroy(depth);
      depth = nullptr;
    }
    if (color != nullptr) {
      engine->destroy(color);
      color = nullptr;
    }
    size = 0u;
  }

  bool resize(std::uint32_t requested_size) {
    if (size == requested_size && target != nullptr) {
      return true;
    }
    preview.resize(requested_size, requested_size);
    destroy_target();
    color = filament::Texture::Builder()
                .width(requested_size)
                .height(requested_size)
                .levels(1u)
                .format(filament::Texture::InternalFormat::RGBA8)
                .usage(filament::Texture::Usage::COLOR_ATTACHMENT |
                       filament::Texture::Usage::SAMPLEABLE |
                       filament::Texture::Usage::BLIT_SRC)
                .build(*engine);
    depth = filament::Texture::Builder()
                .width(requested_size)
                .height(requested_size)
                .levels(1u)
                .format(filament::Texture::InternalFormat::DEPTH24)
                .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
                .build(*engine);
    if (color == nullptr || depth == nullptr) {
      destroy_target();
      return false;
    }
    target = filament::RenderTarget::Builder()
                 .texture(filament::RenderTarget::AttachmentPoint::COLOR, color)
                 .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depth)
                 .build(*engine);
    if (target == nullptr) {
      destroy_target();
      return false;
    }
    size = requested_size;
    return true;
  }

  void shutdown() {
    if (engine == nullptr) {
      return;
    }
    engine->flushAndWait();
    engine->pumpMessageQueues();
    pending.reset();
    preview.shutdown();
    destroy_target();
    if (renderer != nullptr) {
      engine->destroy(renderer);
      renderer = nullptr;
    }
    engine = nullptr;
  }

  filament::Engine *engine = nullptr;
  filament::Renderer *renderer = nullptr;
  filament::RenderTarget *target = nullptr;
  filament::Texture *color = nullptr;
  filament::Texture *depth = nullptr;
  MaterialPreviewRenderer preview;
  std::shared_ptr<MaterialThumbnailReadback> pending;
  std::uint32_t size = 0u;
};

MaterialThumbnailRenderer::MaterialThumbnailRenderer()
    : impl_(std::make_unique<Impl>()) {}

MaterialThumbnailRenderer::~MaterialThumbnailRenderer() = default;

bool MaterialThumbnailRenderer::initialize(
    filament::Engine &engine, filament::Material &shaded_material,
    filament::Material &rendered_material, filament::Texture &solid_texture,
    const std::filesystem::path &resource_root) {
  impl_->shutdown();
  impl_->engine = &engine;
  impl_->renderer = engine.createRenderer();
  if (impl_->renderer == nullptr ||
      !impl_->preview.initialize(engine, shaded_material, rendered_material,
                                 solid_texture, resource_root,
                                 MaterialPreviewLightingPreset::AssetThumbnail)) {
    impl_->shutdown();
    return false;
  }
  impl_->renderer->setClearOptions({.clearColor = {0.0, 0.0, 0.0, 0.0},
                                    .clear = true});
  return true;
}

void MaterialThumbnailRenderer::shutdown() { impl_->shutdown(); }

bool MaterialThumbnailRenderer::busy() const noexcept {
  return impl_->pending != nullptr && !impl_->pending->ready();
}

void MaterialThumbnailRenderer::poll() {
  if (impl_->engine != nullptr && busy()) {
    impl_->engine->pumpMessageQueues();
  }
}

std::shared_ptr<const MaterialThumbnailReadback>
MaterialThumbnailRenderer::submit(RenderMaterialEvaluationSnapshot snapshot,
                                  std::uint32_t size) {
  if (impl_->engine == nullptr || busy() || size == 0u ||
      size > kMaximumSize || snapshot.standard_material() == nullptr ||
      !impl_->resize(size)) {
    return {};
  }
  impl_->pending = std::make_shared<MaterialThumbnailReadback>();
  impl_->pending->size = size;
  impl_->pending->rgba8.resize(static_cast<std::size_t>(size) * size * 4u);
  impl_->preview.set_material(std::move(snapshot));
  impl_->preview.render_offscreen(*impl_->renderer, *impl_->target);

  // The descriptor retains the allocation even if a project switch drops the
  // UI handle. No driver callback captures a renderer, widget, or API session.
  auto *holder = new std::shared_ptr<MaterialThumbnailReadback>(impl_->pending);
  impl_->renderer->readPixels(
      impl_->target, 0u, 0u, size, size,
      filament::backend::PixelBufferDescriptor(
          impl_->pending->rgba8.data(), impl_->pending->rgba8.size(),
          filament::backend::PixelDataFormat::RGBA,
          filament::backend::PixelDataType::UBYTE,
          [](void *, std::size_t, void *user) {
            auto *readback =
                static_cast<std::shared_ptr<MaterialThumbnailReadback> *>(user);
            (*readback)->complete.store(true, std::memory_order_release);
            delete readback;
          },
          holder));
  impl_->engine->flush();
  return impl_->pending;
}

} // namespace quader

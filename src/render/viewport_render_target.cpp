////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_render_target.hpp"

#include "core/logger.hpp"

#include <filament/Viewport.h>

#include <algorithm>
#include <cstdint>

namespace quader {
namespace {

constexpr std::uint32_t kViewportRenderTargetCapacityStep = 128u;

std::uint32_t viewport_render_target_capacity_for_size(std::uint32_t size) {
  size = std::max(1u, size);
  return ((size + kViewportRenderTargetCapacityStep - 1u) /
          kViewportRenderTargetCapacityStep) *
         kViewportRenderTargetCapacityStep;
}

/**
 * Rolls back render-target textures and target allocation until resize records ownership.
 */
struct RenderTargetCreationGuard {
    filament::Engine& engine;
    filament::Texture *color_texture = nullptr;
    filament::Texture *depth_texture = nullptr;
    filament::RenderTarget *render_target = nullptr;
    bool released = false;

    ~RenderTargetCreationGuard() {
        if (released) {
            return;
        }
        if (render_target) {
          engine.destroy(render_target);
        }
        if (color_texture) {
          engine.destroy(color_texture);
        }
        if (depth_texture) {
          engine.destroy(depth_texture);
        }
    }

    void release() {
        released = true;
    }
};

} // namespace

ViewportRenderTarget::~ViewportRenderTarget() {
    shutdown();
}

void ViewportRenderTarget::initialize(filament::Engine &engine,
                                      TextureRegistry &texture_registry) {
  engine_ = &engine;
  texture_registry_ = &texture_registry;
}

void ViewportRenderTarget::shutdown() {
    if (!engine_) {
        return;
    }

    destroy();
    engine_ = nullptr;
    texture_registry_ = nullptr;
}

void ViewportRenderTarget::resize(std::uint32_t width, std::uint32_t height,
                                  filament::View *view,
                                  TextureSampleColorSpace sample_color_space) {
  if (!engine_ || !texture_registry_ || !view) {
    return;
  }

  viewport_width_ = std::max(1u, width);
  viewport_height_ = std::max(1u, height);
  const std::uint32_t capacity_width =
      viewport_render_target_capacity_for_size(viewport_width_);
  const std::uint32_t capacity_height =
      viewport_render_target_capacity_for_size(viewport_height_);
  if (render_target_ && capacity_width == width_ &&
      capacity_height == height_) {
    if (sample_color_space_ != sample_color_space) {
      sample_color_space_ = sample_color_space;
      texture_registry_->update_texture(texture_handle_, color_texture_,
                                        sample_color_space_);
    }
    view->setRenderTarget(render_target_);
    view->setViewport({0, 0, viewport_width_, viewport_height_});
    return;
  }

  destroy_render_resources();
  width_ = capacity_width;
  height_ = capacity_height;
  sample_color_space_ = sample_color_space;

  using Texture = filament::Texture;
  LOG_INFO("Allocating viewport render target {}x{} for viewport {}x{}", width_,
           height_, viewport_width_, viewport_height_);
  RenderTargetCreationGuard creation_guard{*engine_};
  creation_guard.color_texture =
      Texture::Builder()
          .width(width_)
          .height(height_)
          .levels(1)
          .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
          .format(Texture::InternalFormat::RGBA8)
          .build(*engine_);
  if (creation_guard.color_texture == nullptr) {
    return;
  }

  creation_guard.depth_texture = Texture::Builder()
                                     .width(width_)
                                     .height(height_)
                                     .levels(1)
                                     .usage(Texture::Usage::DEPTH_ATTACHMENT)
                                     .format(Texture::InternalFormat::DEPTH24)
                                     .build(*engine_);
  if (creation_guard.depth_texture == nullptr) {
    return;
  }

  creation_guard.render_target =
      filament::RenderTarget::Builder()
          .texture(filament::RenderTarget::AttachmentPoint::COLOR,
                   creation_guard.color_texture)
          .texture(filament::RenderTarget::AttachmentPoint::DEPTH,
                   creation_guard.depth_texture)
          .build(*engine_);
  if (creation_guard.render_target == nullptr) {
    return;
  }

  if (texture_handle_ == kInvalidRenderTextureHandle) {
    texture_handle_ = texture_registry_->register_texture(
        creation_guard.color_texture, sample_color_space_);
  } else {
    texture_registry_->update_texture(
        texture_handle_, creation_guard.color_texture, sample_color_space_);
  }
  color_texture_ = creation_guard.color_texture;
  depth_texture_ = creation_guard.depth_texture;
  render_target_ = creation_guard.render_target;
  view->setRenderTarget(render_target_);
  view->setViewport({0, 0, viewport_width_, viewport_height_});
  LOG_INFO("Viewport render target ready {}x{} for viewport {}x{} as texture "
           "handle {}",
           width_, height_, viewport_width_, viewport_height_, texture_handle_);
  creation_guard.release();
}

void ViewportRenderTarget::set_sample_color_space(
    TextureSampleColorSpace sample_color_space) {
  if (sample_color_space_ == sample_color_space) {
    return;
  }
  sample_color_space_ = sample_color_space;
  if (texture_registry_ && texture_handle_ != kInvalidRenderTextureHandle &&
      color_texture_) {
    texture_registry_->update_texture(texture_handle_, color_texture_,
                                      sample_color_space_);
  }
}

void ViewportRenderTarget::destroy() {
  if (texture_registry_ && texture_handle_ != kInvalidRenderTextureHandle) {
    texture_registry_->unregister_texture(texture_handle_);
    texture_handle_ = kInvalidRenderTextureHandle;
  }
  destroy_render_resources();
}

void ViewportRenderTarget::destroy_render_resources() {
  if (!engine_) {
    return;
  }
  if (render_target_) {
    engine_->destroy(render_target_);
    render_target_ = nullptr;
  }
  if (color_texture_) {
    engine_->destroy(color_texture_);
    color_texture_ = nullptr;
  }
  if (depth_texture_) {
    engine_->destroy(depth_texture_);
    depth_texture_ = nullptr;
  }
}

} // namespace quader

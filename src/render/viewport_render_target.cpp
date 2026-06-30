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

namespace quader {
namespace {

/**
 * Rolls back render-target textures and target allocation until resize records ownership.
 */
struct RenderTargetCreationGuard {
    filament::Engine& engine;
    filament::Texture* colorTexture = nullptr;
    filament::Texture* depthTexture = nullptr;
    filament::RenderTarget* renderTarget = nullptr;
    bool released = false;

    ~RenderTargetCreationGuard() {
        if (released) {
            return;
        }
        if (renderTarget) {
            engine.destroy(renderTarget);
        }
        if (colorTexture) {
            engine.destroy(colorTexture);
        }
        if (depthTexture) {
            engine.destroy(depthTexture);
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

void ViewportRenderTarget::initialize(filament::Engine& engine, TextureRegistry& textureRegistry) {
    engine_ = &engine;
    textureRegistry_ = &textureRegistry;
}

void ViewportRenderTarget::shutdown() {
    if (!engine_) {
        return;
    }

    destroy();
    engine_ = nullptr;
    textureRegistry_ = nullptr;
}

void ViewportRenderTarget::resize(
        std::uint32_t width,
        std::uint32_t height,
        filament::View* view,
        TextureSampleColorSpace sampleColorSpace) {
    if (!engine_ || !textureRegistry_ || !view) {
        return;
    }

    width = std::max(1u, width);
    height = std::max(1u, height);
    if (renderTarget_ && width == width_ && height == height_) {
        if (sampleColorSpace_ != sampleColorSpace) {
            sampleColorSpace_ = sampleColorSpace;
            textureRegistry_->updateTexture(textureHandle_, colorTexture_, sampleColorSpace_);
        }
        view->setRenderTarget(renderTarget_);
        view->setViewport({0, 0, width_, height_});
        return;
    }

    destroyRenderResources();
    width_ = width;
    height_ = height;
    sampleColorSpace_ = sampleColorSpace;

    using Texture = filament::Texture;
    LOG_INFO("Allocating viewport render target {}x{}", width, height);
    RenderTargetCreationGuard creationGuard{*engine_};
    creationGuard.colorTexture = Texture::Builder()
            .width(width_)
            .height(height_)
            .levels(1)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine_);
    if (creationGuard.colorTexture == nullptr) {
        return;
    }

    creationGuard.depthTexture = Texture::Builder()
            .width(width_)
            .height(height_)
            .levels(1)
            .usage(Texture::Usage::DEPTH_ATTACHMENT)
            .format(Texture::InternalFormat::DEPTH24)
            .build(*engine_);
    if (creationGuard.depthTexture == nullptr) {
        return;
    }

    creationGuard.renderTarget = filament::RenderTarget::Builder()
            .texture(filament::RenderTarget::AttachmentPoint::COLOR, creationGuard.colorTexture)
            .texture(filament::RenderTarget::AttachmentPoint::DEPTH, creationGuard.depthTexture)
            .build(*engine_);
    if (creationGuard.renderTarget == nullptr) {
        return;
    }

    if (textureHandle_ == InvalidRenderTextureHandle) {
        textureHandle_ = textureRegistry_->registerTexture(creationGuard.colorTexture, sampleColorSpace_);
    } else {
        textureRegistry_->updateTexture(textureHandle_, creationGuard.colorTexture, sampleColorSpace_);
    }
    colorTexture_ = creationGuard.colorTexture;
    depthTexture_ = creationGuard.depthTexture;
    renderTarget_ = creationGuard.renderTarget;
    view->setRenderTarget(renderTarget_);
    view->setViewport({0, 0, width_, height_});
    LOG_INFO("Viewport render target ready {}x{} as texture handle {}", width_, height_, textureHandle_);
    creationGuard.release();
}

void ViewportRenderTarget::destroy() {
    if (textureRegistry_ && textureHandle_ != InvalidRenderTextureHandle) {
        textureRegistry_->unregisterTexture(textureHandle_);
        textureHandle_ = InvalidRenderTextureHandle;
    }
    destroyRenderResources();
}

void ViewportRenderTarget::destroyRenderResources() {
    if (!engine_) {
        return;
    }
    if (renderTarget_) {
        engine_->destroy(renderTarget_);
        renderTarget_ = nullptr;
    }
    if (colorTexture_) {
        engine_->destroy(colorTexture_);
        colorTexture_ = nullptr;
    }
    if (depthTexture_) {
        engine_->destroy(depthTexture_);
        depthTexture_ = nullptr;
    }
}

} // namespace quader

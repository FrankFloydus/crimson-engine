#include "render/ViewportRenderTarget.h"

#include "core/Logger.h"

#include <filament/Viewport.h>

#include <algorithm>

namespace quader {

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

void ViewportRenderTarget::resize(std::uint32_t width, std::uint32_t height, filament::View* view) {
    if (!engine_ || !textureRegistry_ || !view) {
        return;
    }

    width = std::max(1u, width);
    height = std::max(1u, height);
    if (renderTarget_ && width == width_ && height == height_) {
        return;
    }

    destroy();
    width_ = width;
    height_ = height;

    using Texture = filament::Texture;
    LOG_INFO("Allocating viewport render target {}x{}", width_, height_);
    colorTexture_ = Texture::Builder()
            .width(width_)
            .height(height_)
            .levels(1)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine_);

    depthTexture_ = Texture::Builder()
            .width(width_)
            .height(height_)
            .levels(1)
            .usage(Texture::Usage::DEPTH_ATTACHMENT)
            .format(Texture::InternalFormat::DEPTH24)
            .build(*engine_);

    renderTarget_ = filament::RenderTarget::Builder()
            .texture(filament::RenderTarget::AttachmentPoint::COLOR, colorTexture_)
            .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depthTexture_)
            .build(*engine_);

    textureId_ = textureRegistry_->registerTexture(colorTexture_, TextureSampleColorSpace::LinearSrgb);
    view->setRenderTarget(renderTarget_);
    view->setViewport({0, 0, width_, height_});
    LOG_INFO("Viewport render target ready {}x{} as texture id {}", width_, height_, textureId_);
}

void ViewportRenderTarget::destroy() {
    if (textureRegistry_ && textureId_ != ImTextureID_Invalid) {
        textureRegistry_->unregisterTexture(textureId_);
        textureId_ = ImTextureID_Invalid;
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

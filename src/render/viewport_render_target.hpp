////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/texture_registry.hpp"

#include <filament/Engine.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>
#include <filament/View.h>

#include <cstdint>

namespace quader {

/**
 * Owns Viewport Render Target behavior within the render extraction and Filament resource layer.
 */
class ViewportRenderTarget {
public:
    ViewportRenderTarget() = default;
    ViewportRenderTarget(const ViewportRenderTarget&) = delete;
    ViewportRenderTarget& operator=(const ViewportRenderTarget&) = delete;
    ViewportRenderTarget(ViewportRenderTarget&&) = delete;
    ViewportRenderTarget& operator=(ViewportRenderTarget&&) = delete;
    ~ViewportRenderTarget();

    void initialize(filament::Engine& engine, TextureRegistry& textureRegistry);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && textureRegistry_ != nullptr;
    }

    void resize(std::uint32_t width,
            std::uint32_t height,
            filament::View* view,
            TextureSampleColorSpace sampleColorSpace = TextureSampleColorSpace::LinearSrgb);
    void destroy();

    filament::RenderTarget* renderTarget() const { return renderTarget_; }
    RenderTextureHandle textureHandle() const { return textureHandle_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

private:
    void destroyRenderResources();

    filament::Engine* engine_ = nullptr;
    TextureRegistry* textureRegistry_ = nullptr;
    filament::Texture* colorTexture_ = nullptr;
    filament::Texture* depthTexture_ = nullptr;
    filament::RenderTarget* renderTarget_ = nullptr;
    RenderTextureHandle textureHandle_ = InvalidRenderTextureHandle;
    TextureSampleColorSpace sampleColorSpace_ = TextureSampleColorSpace::LinearSrgb;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
};

} // namespace quader

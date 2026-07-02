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

    void initialize(filament::Engine &engine,
                    TextureRegistry &texture_registry);
    void shutdown();

    bool initialized() const {
      return engine_ != nullptr && texture_registry_ != nullptr;
    }

    void resize(std::uint32_t width, std::uint32_t height, filament::View *view,
                TextureSampleColorSpace sample_color_space =
                    TextureSampleColorSpace::LinearSrgb);
    void destroy();

    filament::RenderTarget *render_target() const { return render_target_; }
    RenderTextureHandle texture_handle() const { return texture_handle_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    std::uint32_t viewport_width() const { return viewport_width_; }
    std::uint32_t viewport_height() const { return viewport_height_; }

  private:
    void destroy_render_resources();

    filament::Engine* engine_ = nullptr;
    TextureRegistry *texture_registry_ = nullptr;
    filament::Texture *color_texture_ = nullptr;
    filament::Texture *depth_texture_ = nullptr;
    filament::RenderTarget *render_target_ = nullptr;
    RenderTextureHandle texture_handle_ = kInvalidRenderTextureHandle;
    TextureSampleColorSpace sample_color_space_ =
        TextureSampleColorSpace::LinearSrgb;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
    std::uint32_t viewport_width_ = 1;
    std::uint32_t viewport_height_ = 1;
};

} // namespace quader

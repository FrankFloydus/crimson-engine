////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/RmlFilamentRenderer.h"
#include "render/TextureRegistry.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Rml {
class Context;
class ElementDocument;
class EventListener;
class FileInterface;
class SystemInterface;
} // namespace Rml

namespace quader {

class RenderSystem;

/**
 * Owns the active RmlUi context, document, resource loading, and render bridge.
 */
class RmlUiContextHost {
public:
    RmlUiContextHost();
    ~RmlUiContextHost();

    bool initialize(
            SDL_Window* window,
            RenderSystem& renderSystem,
            const std::filesystem::path& executableDir,
            std::string utilityRcss,
            std::string debugPaletteRml,
            std::string initialTextureSource,
            RenderTextureHandle initialTextureHandle,
            std::uint32_t initialTextureWidth,
            std::uint32_t initialTextureHeight);
    void shutdown();

    bool processEvent(const SDL_Event& event, float windowToFramebufferScaleX, float windowToFramebufferScaleY);
    void setDensityIndependentPixelRatio(float dpRatio);
    void prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void render(RenderSystem& renderSystem);

    void registerTextureSource(
            std::string source,
            RenderTextureHandle textureHandle,
            std::uint32_t width,
            std::uint32_t height);
    void clearTextureSources();

    void setElementDisplay(const std::string& elementId, const std::string& display);
    [[nodiscard]] bool initialized() const { return initialized_; }

private:
    SDL_Window* window_ = nullptr;
    RmlFilamentRenderer renderer_;
    std::unique_ptr<Rml::SystemInterface> systemInterface_;
    std::unique_ptr<Rml::FileInterface> fileInterface_;
    std::unique_ptr<Rml::EventListener> debugPaletteCopyListener_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    float dpRatio_ = 1.0f;
    bool initialized_ = false;
    bool rmlInitialized_ = false;
};

} // namespace quader

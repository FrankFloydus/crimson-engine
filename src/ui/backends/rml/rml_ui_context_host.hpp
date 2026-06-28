////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/rml_filament_renderer.hpp"
#include "render/texture_registry.hpp"
#include "ui/components/atoms/ui_dropdown_component.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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

/// Stores an RmlUi element rectangle in framebuffer pixels.
struct RmlElementRect {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

/**
 * Enumerates window chrome actions requested by RmlUi controls.
 */
enum class WindowChromeAction {
    Minimize,
    RestoreOrMaximize,
    Close,
};

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
            std::string iconRcss,
            std::string debugPaletteRml,
            std::string initialTextureSource,
            RenderTextureHandle initialTextureHandle,
            std::uint32_t initialTextureWidth,
            std::uint32_t initialTextureHeight);
    void shutdown();

    bool processEvent(const SDL_Event& event, float windowToFramebufferScaleX, float windowToFramebufferScaleY);
    void setDensityIndependentPixelRatio(float dpRatio);
    void updateLayout(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void render(RenderSystem& renderSystem);

    void registerTextureSource(
            std::string source,
            RenderTextureHandle textureHandle,
            std::uint32_t width,
            std::uint32_t height);
    void clearTextureSources();

    void setElementDisplay(const std::string& elementId, const std::string& display);
    void setElementInnerRml(const std::string& elementId, const std::string& innerRml);
    [[nodiscard]] std::optional<RmlElementRect> measureElementById(const std::string& elementId) const;
    [[nodiscard]] std::optional<WindowChromeAction> takeWindowChromeAction();
    [[nodiscard]] std::optional<std::string> takeDropdownToggle();
    [[nodiscard]] std::optional<UIDropdownSelection> takeDropdownSelection();
    [[nodiscard]] std::optional<std::string> takeModelingModeShelfControlSelection();
    [[nodiscard]] std::optional<std::string> takeModelingModeShelfCommand();
    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] bool debuggerVisible() const;
    [[nodiscard]] bool textInputActive() const;

private:
    SDL_Window* window_ = nullptr;
    RmlFilamentRenderer renderer_;
    std::unique_ptr<Rml::SystemInterface> systemInterface_;
    std::unique_ptr<Rml::FileInterface> fileInterface_;
    std::unique_ptr<Rml::EventListener> debugPaletteCopyListener_;
    std::unique_ptr<Rml::EventListener> windowChromeActionListener_;
    std::unique_ptr<Rml::EventListener> dropdownSelectionListener_;
    std::unique_ptr<Rml::EventListener> modelingModeShelfControlSelectionListener_;
    Rml::Context* context_ = nullptr;
    Rml::Context* debuggerContext_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    std::optional<WindowChromeAction> pendingWindowChromeAction_;
    std::optional<std::string> pendingDropdownToggle_;
    std::optional<UIDropdownSelection> pendingDropdownSelection_;
    std::optional<std::string> pendingModelingModeShelfControlSelection_;
    std::optional<std::string> pendingModelingModeShelfCommand_;
    float dpRatio_ = 1.0f;
    bool initialized_ = false;
    bool rmlInitialized_ = false;
    bool rmlDebuggerInitialized_ = false;
};

} // namespace quader

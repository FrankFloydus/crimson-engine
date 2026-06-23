////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "core/FrameTiming.h"
#include "render/RenderSystem.h"
#include "render/SceneRenderBridge.h"
#include "scene/SceneDocument.h"
#include "ui/backends/rml/RmlUiContextHost.h"
#include "ui/controllers/ViewportHost.h"
#include "ui/UISettings.h"
#include "viewport/ViewportSettings.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>

namespace quader {

class QuaderEditorApi;
class EditorSelectionService;
class ModelingSessionService;

/**
 * Owns the RmlUi-backed viewport shell for the native app.
 */
class UISystem {
public:
    UISystem();
    ~UISystem();

    void initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir);
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame(const FrameTiming& frameTiming);
    void draw(QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            ModelingSessionService& modeling,
            EditorSelectionService& selection,
            AssetId defaultMaterial,
            bool& quitRequested);
    void prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void prepareViewports(
            QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            const SceneDocument& document,
            const AssetRegistry& assets);
    void renderViewports(RenderSystem& renderSystem);
    void renderWindowedViewports(RenderSystem& renderSystem);
    void render(RenderSystem& renderSystem);
    void installRuntimeHooks(QuaderEditorApi& editor);
    const ViewportToolState& viewportToolState() const { return viewportHost_.viewportToolState(); }

private:
    void updateWindowMetrics();
    void updateRmlDensityScale();
    void saveViewportSettings();
    void toggleDebugPalette();
    void applyDebugPaletteVisibility();

    SDL_Window* window_ = nullptr;
    RmlUiContextHost rmlHost_;
    ViewportHost viewportHost_;
    UISettingsPaths settingsPaths_;
    ViewportSettings viewportSettings_;
    std::uint32_t framebufferWidth_ = 1u;
    std::uint32_t framebufferHeight_ = 1u;
    int windowWidth_ = 1;
    int windowHeight_ = 1;
    float rmlDpRatio_ = 1.0f;
    bool initialized_ = false;
    bool debugPaletteVisible_ = false;
};

} // namespace quader

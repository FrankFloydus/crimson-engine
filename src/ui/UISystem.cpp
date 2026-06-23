////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/UISystem.h"

#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "ui/DebugPaletteView.h"
#include "ui/UIUtilityTokens.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

QuaderApiVoidResult runtimeStatus(bool changed, std::string message = {}) {
    return {
            .ok = true,
            .changed = changed,
            .message = std::move(message),
            .mutationKind = changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
    };
}

QuaderApiVoidResult runtimeFailure(std::string message) {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
    };
}

} // namespace

UISystem::UISystem() = default;

UISystem::~UISystem() {
    shutdown();
}

void UISystem::initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir) {
    window_ = window;
    updateWindowMetrics();

    settingsPaths_ = UISettings::resolvePaths(executableDir);
    std::error_code error;
    std::filesystem::create_directories(settingsPaths_.directory, error);
    viewportSettings_ = ViewportSettings::load(settingsPaths_.viewportSettingsFile);

    viewportHost_.initialize(window_, renderSystem, viewportSettings_);

    const std::vector<DebugPaletteColorToken> debugPaletteColors =
            loadDebugPaletteColorTokens(appPaletteTokenPath(executableDir));
    const std::vector<UIUtilityToken> utilityTokens =
            loadUIUtilityTokens(appUtilityTokenPath(executableDir));
    const std::string utilityRcss = buildUIUtilityRcss(utilityTokens, debugPaletteColors);
    const std::string debugPaletteRml = buildDebugPalettePageRml(debugPaletteColors);

    if (!rmlHost_.initialize(window_,
                renderSystem,
                executableDir,
                utilityRcss,
                debugPaletteRml,
                viewportHost_.textureSource(),
                viewportHost_.textureHandle(),
                viewportHost_.textureWidth(),
                viewportHost_.textureHeight())) {
        shutdown();
        return;
    }
    updateRmlDensityScale();
    applyDebugPaletteVisibility();

    initialized_ = true;
    LOG_INFO("RmlUi viewport shell initialized");
}

void UISystem::shutdown() {
    if (!initialized_ && !rmlHost_.initialized()) {
        return;
    }

    viewportHost_.setCursorCaptured(false);
    saveViewportSettings();
    rmlHost_.shutdown();
    viewportHost_.shutdown();
    initialized_ = false;
    window_ = nullptr;
}

bool UISystem::processEvent(const SDL_Event& event) {
    if (!window_) {
        return false;
    }

    updateWindowMetrics();
    if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.key == SDLK_F1 &&
            !event.key.repeat &&
            (SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
        toggleDebugPalette();
        return true;
    }

    if (viewportHost_.processEvent(event)) {
        return true;
    }

    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    (void)rmlHost_.processEvent(event, scaleX, scaleY);
    return false;
}

void UISystem::beginFrame(const FrameTiming& frameTiming) {
    updateWindowMetrics();
    viewportHost_.beginFrame(frameTiming);
}

void UISystem::draw(QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        SceneDocument& document,
        AssetRegistry& assets,
        ModelingSessionService& modeling,
        EditorSelectionService& selection,
        AssetId defaultMaterial,
        bool& quitRequested) {
    (void)renderBridge;
    (void)modeling;
    (void)selection;
    (void)defaultMaterial;
    (void)quitRequested;

    viewportHost_.handleFrame(editor, document, assets, debugPaletteVisible_);
}

void UISystem::prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    framebufferWidth_ = std::max(1u, framebufferWidth);
    framebufferHeight_ = std::max(1u, framebufferHeight);
    viewportHost_.resize(framebufferWidth_, framebufferHeight_);
    rmlHost_.clearTextureSources();
    rmlHost_.registerTextureSource(
            viewportHost_.textureSource(),
            viewportHost_.textureHandle(),
            viewportHost_.textureWidth(),
            viewportHost_.textureHeight());
    rmlHost_.prepareRenderData(framebufferWidth_, framebufferHeight_);
}

void UISystem::prepareViewports(
        QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    viewportHost_.prepareViewports(editor, renderBridge, document, assets);
}

void UISystem::renderViewports(RenderSystem& renderSystem) {
    viewportHost_.renderViewports(renderSystem);
}

void UISystem::renderWindowedViewports(RenderSystem& renderSystem) {
    viewportHost_.renderWindowedViewports(renderSystem);
}

void UISystem::render(RenderSystem& renderSystem) {
    rmlHost_.render(renderSystem);
}

void UISystem::installRuntimeHooks(QuaderEditorApi& editor) {
    editor.ui().setRuntimeHooks(QuaderUIRuntimeHooks{
            .isPanelVisible = [](std::string_view panel) {
                return panel == "Viewport";
            },
            .setPanelVisible = [](std::string_view panel, bool visible) {
                if (panel != "Viewport") {
                    return runtimeFailure("Only the viewport panel is available in the RmlUi shell.");
                }
                return runtimeStatus(false, visible ? "Viewport is always visible." : "Viewport cannot be hidden.");
            },
            .focusPanel = [](std::string_view panel) {
                if (panel != "Viewport") {
                    return runtimeFailure("Only the viewport panel is available in the RmlUi shell.");
                }
                return runtimeStatus(false);
            },
            .notify = [](QuaderNotification) {
                return runtimeStatus(false);
            },
    });
    editor.preferences().setRuntimeHooks(QuaderPreferencesRuntimeHooks{
            .locale = []() {
                return std::string("en-US");
            },
            .setLocale = [](std::string) {
                return runtimeFailure("Locale selection is not exposed in the RmlUi viewport shell yet.");
            },
            .save = [this]() {
                saveViewportSettings();
                return runtimeStatus(false, "Preferences saved.");
            },
            .navigationGizmoVisible = []() {
                return false;
            },
            .setNavigationGizmoVisible = [](bool) {
                return runtimeFailure("Navigation gizmo is not available in the RmlUi viewport shell yet.");
            },
    });
}

void UISystem::updateWindowMetrics() {
    if (!window_) {
        return;
    }

    int framebufferW = 1;
    int framebufferH = 1;
    SDL_GetWindowSizeInPixels(window_, &framebufferW, &framebufferH);
    framebufferWidth_ = static_cast<std::uint32_t>(std::max(1, framebufferW));
    framebufferHeight_ = static_cast<std::uint32_t>(std::max(1, framebufferH));
    SDL_GetWindowSize(window_, &windowWidth_, &windowHeight_);
    windowWidth_ = std::max(1, windowWidth_);
    windowHeight_ = std::max(1, windowHeight_);
    updateRmlDensityScale();
}

void UISystem::updateRmlDensityScale() {
    if (!window_) {
        return;
    }

    const float displayScale = SDL_GetWindowDisplayScale(window_);
    if (!std::isfinite(displayScale) || displayScale <= 0.0f) {
        return;
    }

    const float dpRatio = std::clamp(displayScale, 0.75f, 4.0f);
    if (std::abs(dpRatio - rmlDpRatio_) <= 0.001f) {
        return;
    }

    rmlDpRatio_ = dpRatio;
    rmlHost_.setDensityIndependentPixelRatio(rmlDpRatio_);
}

void UISystem::saveViewportSettings() {
    if (!settingsPaths_.viewportSettingsFile.empty() &&
            !viewportSettings_.save(settingsPaths_.viewportSettingsFile)) {
        LOG_WARN("Unable to save viewport settings to {}", settingsPaths_.viewportSettingsFile.string());
    }
}

void UISystem::toggleDebugPalette() {
    debugPaletteVisible_ = !debugPaletteVisible_;
    applyDebugPaletteVisibility();
    if (debugPaletteVisible_) {
        viewportHost_.setCursorCaptured(false);
    }
}

void UISystem::applyDebugPaletteVisibility() {
    rmlHost_.setElementDisplay("viewport-panel", debugPaletteVisible_ ? "none" : "block");
    rmlHost_.setElementDisplay("debug-palette-page", debugPaletteVisible_ ? "block" : "none");
}

} // namespace quader

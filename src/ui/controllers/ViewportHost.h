////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "core/FrameTiming.h"
#include "core/Types.h"
#include "render/RenderSystem.h"
#include "render/SceneRenderBridge.h"
#include "render/TextureRegistry.h"
#include "render/ViewportRenderer.h"
#include "scene/SceneDocument.h"
#include "editor/viewport/ViewportInteractionController.h"
#include "ui/components/organisms/ViewportComponent.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportSettings.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {

class QuaderEditorApi;
class ModelingSessionService;
class ViewportWorkspaceService;

/// Stores the measured viewport frame in framebuffer pixels.
struct ViewportFrameRect {
    int left = 0;
    int top = 0;
    std::uint32_t width = 1u;
    std::uint32_t height = 1u;
};

/**
 * Coordinates UI viewport components, render targets, texture sources, and viewport input.
 */
class ViewportHost {
public:
    ViewportHost();
    ~ViewportHost();

    void initialize(SDL_Window* window, RenderSystem& renderSystem, const ViewportSettings& settings);
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame(const FrameTiming& frameTiming);
    void resize(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void setMainViewportFrame(int left, int top, std::uint32_t width, std::uint32_t height);
    void handleFrame(
            QuaderEditorApi& editor,
            SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            bool inputSuppressed);
    void prepareViewports(
            QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            const SceneDocument& document,
            const AssetRegistry& assets);
    void renderViewports(RenderSystem& renderSystem);
    void renderWindowedViewports(RenderSystem& renderSystem);
    void setViewportSettings(const ViewportSettings& settings);
    void setCursorCaptured(bool captured);

    [[nodiscard]] const ViewportToolState& viewportToolState() const { return inputRouter_.state(); }
    [[nodiscard]] std::string textureSource() const;
    [[nodiscard]] RenderTextureHandle textureHandle() const;
    [[nodiscard]] std::uint32_t textureWidth() const;
    [[nodiscard]] std::uint32_t textureHeight() const;
    [[nodiscard]] std::uint32_t framebufferWidth() const { return framebufferWidth_; }
    [[nodiscard]] std::uint32_t framebufferHeight() const { return framebufferHeight_; }
    [[nodiscard]] int windowWidth() const { return windowWidth_; }
    [[nodiscard]] int windowHeight() const { return windowHeight_; }

private:
    struct ViewportWindowInstance;

    void updateWindowMetrics();
    [[nodiscard]] bool mainViewportContainsWindowPoint(float windowX, float windowY) const;
    [[nodiscard]] QVec2 mainViewportWindowPointToLocal(QVec2 windowPoint) const;
    ViewportInputFrame buildViewportInputFrame() const;
    ViewportModifiers currentModifiers() const;
    void clearTransientInput();
    void spawnWindowedViewport();
    void shutdownWindowedViewports();
    bool processWindowedViewportEvent(const SDL_Event& event);
    void handleWindowedViewportFrames(
            QuaderEditorApi& editor,
            ViewportWorkspaceService& workspace,
            SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling,
            AssetId defaultMaterial);
    void prepareWindowedViewports(ViewportWorkspaceService& workspace);
    ViewportInputFrame buildWindowedViewportInputFrame(const ViewportWindowInstance& instance) const;
    void setWindowedViewportCursorCaptured(ViewportWindowInstance& instance, bool captured);
    void clearWindowedViewportTransientInput(ViewportWindowInstance& instance);
    std::uint8_t nextWindowedViewportLayerIndex() const;
    void shutdownWindowedViewport(ViewportWindowInstance& instance);

    SDL_Window* window_ = nullptr;
    RenderSystem* renderSystem_ = nullptr;
    ViewportComponent mainViewport_ = mainViewportComponent();
    ViewportRenderer renderer_;
    ViewportSettings settings_;
    ViewportInputRouter inputRouter_;
    FrameTiming currentFrameTiming_ = {};
    std::uint32_t framebufferWidth_ = 1u;
    std::uint32_t framebufferHeight_ = 1u;
    ViewportFrameRect mainViewportFrame_;
    int windowWidth_ = 1;
    int windowHeight_ = 1;
    QVec2 mouseWindow_ = {};
    QVec2 mouseDeltaWindow_ = {};
    QVec2 relativeMouseDelta_ = {};
    bool middleDown_ = false;
    bool middlePressed_ = false;
    bool middleReleased_ = false;
    bool leftDown_ = false;
    bool leftPressed_ = false;
    bool leftReleased_ = false;
    QVec2 leftClickWindow_ = {};
    int leftClickCount_ = 1;
    bool rightDown_ = false;
    bool rightPressed_ = false;
    bool rightReleased_ = false;
    float wheelY_ = 0.0f;
    std::unordered_set<ViewportKey> keysDown_;
    std::vector<ViewportKey> pressedKeys_;
    std::unordered_set<QuaderMouseButton> mouseButtonsDown_;
    std::unordered_map<QuaderMouseButton, QuaderInputChord> mouseDragChordsDown_;
    std::vector<QuaderInputChord> pressedInputChords_;
    std::vector<QuaderInputChord> activeInputChords_;
    std::vector<QuaderInputChord> releasedInputChords_;
    ViewportInteractionController viewportInteractions_;
    bool relativeMouseCaptured_ = false;
    bool relativeMouseCaptureWarningIssued_ = false;
    std::vector<std::unique_ptr<ViewportWindowInstance>> windowedViewports_;
};

} // namespace quader

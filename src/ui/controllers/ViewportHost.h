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
struct ViewportWorkspaceAuxiliaryViewportState;
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
    void setWindowedViewportTitleFormat(std::string titleFormat);
    void setTextInputActive(bool active);

    [[nodiscard]] const ViewportToolState& viewportToolState() const { return inputRouter_.state(); }
    [[nodiscard]] std::string textureSource() const;
    [[nodiscard]] RenderTextureHandle textureHandle() const;
    [[nodiscard]] std::uint32_t textureWidth() const;
    [[nodiscard]] std::uint32_t textureHeight() const;
    [[nodiscard]] std::uint32_t framebufferWidth() const { return framebufferWidth_; }
    [[nodiscard]] std::uint32_t framebufferHeight() const { return framebufferHeight_; }
    [[nodiscard]] int windowWidth() const { return windowWidth_; }
    [[nodiscard]] int windowHeight() const { return windowHeight_; }
    [[nodiscard]] bool hasActivePointerGesture() const;

private:
    /**
     * Tracks physical modifier key state after SDL events are translated at the viewport host edge.
     */
    struct ViewportModifierKeyState {
        bool leftControl = false;
        bool rightControl = false;
        bool leftGui = false;
        bool rightGui = false;
        bool leftShift = false;
        bool rightShift = false;
        bool leftAlt = false;
        bool rightAlt = false;
    };

    /**
     * Stores one UI input snapshot before it is projected into viewport input.
     */
    struct ViewportInputSurfaceState {
        QVec2 mouseWindow = {};
        QVec2 mouseDeltaWindow = {};
        QVec2 relativeMouseDelta = {};
        bool middleDown = false;
        bool middlePressed = false;
        bool middleReleased = false;
        bool leftDown = false;
        bool leftPressed = false;
        bool leftReleased = false;
        QVec2 leftClickWindow = {};
        int leftClickCount = 1;
        bool rightDown = false;
        bool rightPressed = false;
        bool rightReleased = false;
        float wheelY = 0.0f;
        std::unordered_set<ViewportKey> keysDown;
        std::unordered_set<QuaderKeyCode> keyboardKeysDown;
        ViewportModifierKeyState modifierKeys;
        std::vector<ViewportKey> pressedKeys;
        std::unordered_set<QuaderMouseButton> mouseButtonsDown;
        std::unordered_map<QuaderMouseButton, QuaderInputChord> mouseDragChordsDown;
        std::vector<QuaderInputChord> pressedInputChords;
        std::vector<QuaderInputChord> releasedInputChords;
    };

    struct ViewportWindowInstance;

    void updateWindowMetrics();
    [[nodiscard]] bool mainViewportContainsWindowPoint(float windowX, float windowY) const;
    [[nodiscard]] QVec2 mainViewportWindowPointToLocal(QVec2 windowPoint) const;
    static void accumulateMouseMotion(ViewportInputSurfaceState& input, QVec2 windowPoint, QVec2 windowDelta);
    static void accumulateMouseButton(
            ViewportInputSurfaceState& input,
            std::uint8_t button,
            bool down,
            bool acceptPress,
            QVec2 windowPoint,
            int clickCount,
            ViewportModifiers modifiers);
    static void accumulateMouseWheel(ViewportInputSurfaceState& input, QVec2 windowPoint, float wheelY);
    static void accumulateKey(
            ViewportInputSurfaceState& input,
            QuaderKeyCode keyCode,
            bool down,
            bool repeat,
            ViewportModifiers modifiers);
    static void applyModifierMask(ViewportInputSurfaceState& input, SDL_Keymod modifiers);
    static void accumulateModifierKey(
            ViewportInputSurfaceState& input,
            SDL_Scancode scanCode,
            SDL_Keymod modifiers,
            bool down);
    [[nodiscard]] static bool inputStateHasActivePointerGesture(const ViewportInputSurfaceState& input);
    static void clearTransientInputState(ViewportInputSurfaceState& input);
    ViewportInputFrame buildViewportInputFrame() const;
    ViewportModifiers currentModifiers(const ViewportInputSurfaceState& input) const;
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
    void handleViewportFrameSurface(
            QuaderEditorApi& editor,
            ViewportWorkspaceService& workspace,
            SceneDocument& document,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            const ViewportComponent& component,
            ViewportInputRouter& inputRouter,
            ViewportInteractionController& interactions,
            ViewportCameraController& camera,
            ViewportInputFrame input,
            ViewportInputSurfaceState& inputState,
            QVec2 leftClickLocal,
            ViewportWindowInstance* windowedInstance);
    void prepareWindowedViewports(ViewportWorkspaceService& workspace);
    ViewportWorkspaceAuxiliaryViewportState& ensureWindowedViewportWorkspaceState(
            ViewportWorkspaceService& workspace,
            const ViewportWindowInstance& instance) const;
    void flushRemovedWindowedViewportWorkspaceState(ViewportWorkspaceService& workspace);
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
    ViewportInputSurfaceState mainInput_;
    ViewportInteractionController viewportInteractions_;
    bool relativeMouseCaptured_ = false;
    bool relativeMouseCaptureWarningIssued_ = false;
    bool textInputActive_ = false;
    std::vector<std::unique_ptr<ViewportWindowInstance>> windowedViewports_;
    std::vector<std::string> removedWindowedViewportIds_;
    std::string windowedViewportTitleFormat_ = "Quader Viewport {index}";
};

} // namespace quader

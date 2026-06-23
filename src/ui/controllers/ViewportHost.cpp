////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/ViewportHost.h"

#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "editor/viewport/QuaderViewportApiRuntimeAccess.h"
#include "platform/PlatformWindow.h"
#include "render/RenderLayers.h"
#include "render/ViewportOverlayFrame.h"
#include "scene/SceneBounds.h"
#include "viewport/ViewportCameraAdapter.h"
#include "viewport/ViewportWorkspaceService.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace quader {
namespace {

std::optional<ViewportKey> viewportKeyFromSdlKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_A:
        return ViewportKey::A;
    case SDLK_B:
        return ViewportKey::B;
    case SDLK_C:
        return ViewportKey::C;
    case SDLK_D:
        return ViewportKey::D;
    case SDLK_F:
        return ViewportKey::F;
    case SDLK_H:
        return ViewportKey::H;
    case SDLK_I:
        return ViewportKey::I;
    case SDLK_M:
        return ViewportKey::M;
    case SDLK_P:
        return ViewportKey::P;
    case SDLK_Q:
        return ViewportKey::Q;
    case SDLK_R:
        return ViewportKey::R;
    case SDLK_S:
        return ViewportKey::S;
    case SDLK_T:
        return ViewportKey::T;
    case SDLK_V:
        return ViewportKey::V;
    case SDLK_W:
        return ViewportKey::W;
    case SDLK_X:
        return ViewportKey::X;
    case SDLK_Y:
        return ViewportKey::Y;
    case SDLK_Z:
        return ViewportKey::Z;
    case SDLK_1:
    case SDLK_KP_1:
        return ViewportKey::Number1;
    case SDLK_2:
    case SDLK_KP_2:
        return ViewportKey::Number2;
    case SDLK_3:
    case SDLK_KP_3:
        return ViewportKey::Number3;
    case SDLK_4:
    case SDLK_KP_4:
        return ViewportKey::Number4;
    case SDLK_5:
    case SDLK_KP_5:
        return ViewportKey::Number5;
    case SDLK_HOME:
        return ViewportKey::Home;
    case SDLK_SPACE:
        return ViewportKey::Space;
    case SDLK_F1:
        return ViewportKey::F1;
    case SDLK_F2:
        return ViewportKey::F2;
    case SDLK_F3:
        return ViewportKey::F3;
    case SDLK_F4:
        return ViewportKey::F4;
    case SDLK_F5:
        return ViewportKey::F5;
    case SDLK_F6:
        return ViewportKey::F6;
    case SDLK_F7:
        return ViewportKey::F7;
    case SDLK_F8:
        return ViewportKey::F8;
    case SDLK_F9:
        return ViewportKey::F9;
    case SDLK_F10:
        return ViewportKey::F10;
    case SDLK_ESCAPE:
        return ViewportKey::Escape;
    case SDLK_DELETE:
        return ViewportKey::Delete;
    case SDLK_BACKSPACE:
        return ViewportKey::Backspace;
    default:
        return std::nullopt;
    }
}

void focusDocument(SceneDocument& document, const AssetRegistry& assets, ViewportCameraController& camera) {
    std::optional<SceneBounds> bounds = selectedWorldBounds(document, assets);
    if (!bounds) {
        bounds = documentWorldBounds(document, assets);
    }
    if (bounds) {
        camera.focusBounds(bounds->min, bounds->max);
    } else {
        camera.focusOrigin();
    }
}

std::optional<SDL_WindowID> eventWindowId(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return event.window.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID;
    default:
        return std::nullopt;
    }
}

} // namespace

/**
 * Stores one window-backed viewport component instance.
 */
struct ViewportHost::ViewportWindowInstance {
    PlatformWindow window;
    filament::SwapChain* swapChain = nullptr;
    ViewportComponent component;
    ViewportRenderer renderer;
    ViewportCameraController camera;
    ViewportInputRouter inputRouter;
    FrameTiming frameTiming = {};
    SDL_WindowID windowId = 0u;
    std::uint32_t framebufferWidth = 1u;
    std::uint32_t framebufferHeight = 1u;
    int windowWidth = 1;
    int windowHeight = 1;
    QVec2 mouseWindow = {};
    QVec2 mouseDeltaWindow = {};
    QVec2 relativeMouseDelta = {};
    bool middleDown = false;
    bool middlePressed = false;
    bool middleReleased = false;
    bool rightDown = false;
    bool rightPressed = false;
    bool rightReleased = false;
    float wheelY = 0.0f;
    std::unordered_set<ViewportKey> keysDown;
    std::vector<ViewportKey> pressedKeys;
    bool relativeMouseCaptured = false;
    bool relativeMouseCaptureWarningIssued = false;
};

ViewportHost::ViewportHost() = default;

ViewportHost::~ViewportHost() {
    shutdown();
}

void ViewportHost::initialize(SDL_Window* window, RenderSystem& renderSystem, const ViewportSettings& settings) {
    window_ = window;
    renderSystem_ = &renderSystem;
    settings_ = settings;
    updateWindowMetrics();
    renderer_.initialize(renderSystem.engine(),
            renderSystem.scene(),
            renderSystem.textures(),
            renderSystem.groundGridMaterial(),
            viewportGridLayerMask(mainViewport_.layerIndex));
    renderer_.resize(framebufferWidth_, framebufferHeight_);
}

void ViewportHost::shutdown() {
    shutdownWindowedViewports();
    setCursorCaptured(false);
    renderer_.shutdown();
    window_ = nullptr;
    renderSystem_ = nullptr;
}

bool ViewportHost::processEvent(const SDL_Event& event) {
    if (!window_) {
        return false;
    }

    if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.key == SDLK_F12 &&
            !event.key.repeat) {
        spawnWindowedViewport();
        return true;
    }

    if (processWindowedViewportEvent(event)) {
        return true;
    }

    const std::optional<SDL_WindowID> windowId = eventWindowId(event);
    if (windowId && *windowId != SDL_GetWindowID(window_)) {
        return false;
    }

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mouseWindow_ = {event.motion.x, event.motion.y};
        mouseDeltaWindow_ = mouseDeltaWindow_ + QVec2{event.motion.xrel, event.motion.yrel};
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        mouseWindow_ = {event.button.x, event.button.y};
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            middleDown_ = down;
            middlePressed_ = middlePressed_ || down;
            middleReleased_ = middleReleased_ || !down;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            rightDown_ = down;
            rightPressed_ = rightPressed_ || down;
            rightReleased_ = rightReleased_ || !down;
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        mouseWindow_ = {event.wheel.mouse_x, event.wheel.mouse_y};
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        wheelY_ += event.wheel.y * direction;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const std::optional<ViewportKey> key = viewportKeyFromSdlKey(event.key.key);
        if (key) {
            if (event.key.down) {
                keysDown_.insert(*key);
                if (!event.key.repeat) {
                    pressedKeys_.push_back(*key);
                }
            } else {
                keysDown_.erase(*key);
            }
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void ViewportHost::beginFrame(const FrameTiming& frameTiming) {
    currentFrameTiming_ = frameTiming;
    updateWindowMetrics();
    relativeMouseDelta_ = {};
    if (relativeMouseCaptured_) {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
        const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
        const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
        relativeMouseDelta_ = {deltaX * scaleX, deltaY * scaleY};
    }

    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        instance.frameTiming = frameTiming;
        instance.relativeMouseDelta = {};
        instance.window.refreshPixelSize();
        instance.framebufferWidth = instance.window.widthPixels();
        instance.framebufferHeight = instance.window.heightPixels();
        SDL_GetWindowSize(instance.window.sdlWindow(), &instance.windowWidth, &instance.windowHeight);
        instance.windowWidth = std::max(1, instance.windowWidth);
        instance.windowHeight = std::max(1, instance.windowHeight);
        if (!instance.relativeMouseCaptured) {
            continue;
        }

        float deltaX = 0.0f;
        float deltaY = 0.0f;
        (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
        const float scaleX = static_cast<float>(instance.framebufferWidth) / static_cast<float>(instance.windowWidth);
        const float scaleY = static_cast<float>(instance.framebufferHeight) / static_cast<float>(instance.windowHeight);
        instance.relativeMouseDelta = {deltaX * scaleX, deltaY * scaleY};
    }
}

void ViewportHost::resize(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    framebufferWidth_ = std::max(1u, framebufferWidth);
    framebufferHeight_ = std::max(1u, framebufferHeight);
}

void ViewportHost::handleFrame(
        QuaderEditorApi& editor,
        SceneDocument& document,
        const AssetRegistry& assets,
        bool inputSuppressed) {
    ViewportWorkspaceService& workspace = QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
    workspace.setLayout(ViewportLayoutId::Single);
    workspace.focusPane(mainViewport_.pane);
    workspace.clearPaneVisibility();
    workspace.setPaneVisible(mainViewport_.pane, true);
    workspace.setPaneRenderSize(mainViewport_.pane, {
            static_cast<float>(framebufferWidth_),
            static_cast<float>(framebufferHeight_),
    });

    if (inputSuppressed) {
        setCursorCaptured(false);
        clearTransientInput();
        handleWindowedViewportFrames(workspace, document, assets);
        return;
    }

    inputRouter_.setGridPreset(workspace.gridPreset());
    ViewportCameraController& camera = workspace.camera(mainViewport_.pane);
    const ViewportInputResult result = inputRouter_.handleFrame(buildViewportInputFrame(), camera);
    if (result.gridPresetChanged) {
        workspace.setGridPreset(inputRouter_.state().gridPreset);
    }
    if (result.focusDocumentRequested) {
        focusDocument(document, assets, camera);
    }

    setCursorCaptured(result.flyCursorCaptureActive);
    handleWindowedViewportFrames(workspace, document, assets);
    clearTransientInput();
}

void ViewportHost::prepareViewports(
        QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    (void)document;
    (void)assets;
    ViewportWorkspaceService& workspace = QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
    workspace.setPaneVisible(mainViewport_.pane, true);
    workspace.setPaneRenderSize(mainViewport_.pane, {
            static_cast<float>(framebufferWidth_),
            static_cast<float>(framebufferHeight_),
    });

    ViewportWorkspacePaneState& pane = workspace.pane(mainViewport_.pane);
    renderer_.resize(framebufferWidth_, framebufferHeight_);
    renderer_.updateCamera(pane.camera);
    renderer_.updateGroundGrid(
            pane.camera,
            viewportGridWorldSizeForPreset(workspace.gridPreset()),
            settings_,
            true);
    renderer_.setSceneVisible(true);
    renderer_.setOverlayVisible(false);
    renderBridge.setOverlayFrame(
            viewportPaneIndex(mainViewport_.pane),
            viewportGridLayerMask(mainViewport_.layerIndex),
            ViewportOverlayFrame{},
            ViewportCameraAdapter::overlayCamera(pane.camera, pane.renderSize));
    prepareWindowedViewports(workspace);
}

void ViewportHost::renderViewports(RenderSystem& renderSystem) {
    renderer_.render(renderSystem.renderer());
}

void ViewportHost::renderWindowedViewports(RenderSystem& renderSystem) {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        if (instance.swapChain == nullptr || !renderSystem.beginFrame(instance.swapChain)) {
            continue;
        }

        instance.renderer.render(renderSystem.renderer());
        renderSystem.endFrame();
    }
}

void ViewportHost::setViewportSettings(const ViewportSettings& settings) {
    settings_ = settings;
}

void ViewportHost::setCursorCaptured(bool captured) {
    if (window_ == nullptr || relativeMouseCaptured_ == captured) {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(window_, captured)) {
        if (!relativeMouseCaptureWarningIssued_) {
            LOG_WARN("Unable to {} viewport relative mouse mode: {}",
                    captured ? "enable" : "disable",
                    SDL_GetError());
            relativeMouseCaptureWarningIssued_ = true;
        }
        return;
    }

    relativeMouseCaptured_ = captured;
    relativeMouseCaptureWarningIssued_ = false;
    float discardX = 0.0f;
    float discardY = 0.0f;
    (void)SDL_GetRelativeMouseState(&discardX, &discardY);
    relativeMouseDelta_ = {};
}

std::string ViewportHost::textureSource() const {
    return std::string(mainViewport_.textureSource);
}

RenderTextureHandle ViewportHost::textureHandle() const {
    return renderer_.textureHandle();
}

std::uint32_t ViewportHost::textureWidth() const {
    return renderer_.width();
}

std::uint32_t ViewportHost::textureHeight() const {
    return renderer_.height();
}

void ViewportHost::updateWindowMetrics() {
    if (!window_) {
        return;
    }

    int framebufferW = 1;
    int framebufferH = 1;
    SDL_GetWindowSizeInPixels(window_, &framebufferW, &framebufferH);
    resize(static_cast<std::uint32_t>(std::max(1, framebufferW)), static_cast<std::uint32_t>(std::max(1, framebufferH)));
    SDL_GetWindowSize(window_, &windowWidth_, &windowHeight_);
    windowWidth_ = std::max(1, windowWidth_);
    windowHeight_ = std::max(1, windowHeight_);

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
    mouseWindow_ = {mouseX, mouseY};
    middleDown_ = (buttons & SDL_BUTTON_MMASK) != 0u;
    rightDown_ = (buttons & SDL_BUTTON_RMASK) != 0u;
}

ViewportInputFrame ViewportHost::buildViewportInputFrame() const {
    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    const QVec2 mousePixel = {
            mouseWindow_.x * scaleX,
            mouseWindow_.y * scaleY,
    };
    const QVec2 mouseDelta = relativeMouseCaptured_
            ? relativeMouseDelta_
            : QVec2{mouseDeltaWindow_.x * scaleX, mouseDeltaWindow_.y * scaleY};
    const bool hovered = mousePixel.x >= 0.0f &&
            mousePixel.y >= 0.0f &&
            mousePixel.x <= static_cast<float>(framebufferWidth_) &&
            mousePixel.y <= static_cast<float>(framebufferHeight_);
    ViewportInputFrame input;
    input.viewportHovered = hovered || inputRouter_.navigating();
    input.viewportFocused = true;
    input.textInputActive = false;
    input.middlePressed = middlePressed_ && input.viewportHovered;
    input.middleDown = middleDown_;
    input.middleReleased = middleReleased_;
    input.rightPressed = rightPressed_ && input.viewportHovered;
    input.rightDown = rightDown_;
    input.rightReleased = rightReleased_;
    input.mouseWheel = input.viewportHovered ? wheelY_ : 0.0f;
    input.deltaSeconds = static_cast<float>(std::max(currentFrameTiming_.deltaSeconds, 0.0));
    input.mouseScreen = mousePixel;
    input.mouseLocal = mousePixel;
    input.mouseDelta = mouseDelta;
    input.viewportSize = {
            static_cast<float>(framebufferWidth_),
            static_cast<float>(framebufferHeight_),
    };
    input.modifiers = currentModifiers();
    if (keysDown_.contains(ViewportKey::W)) {
        input.flyLocalDirection.z += 1.0f;
    }
    if (keysDown_.contains(ViewportKey::S)) {
        input.flyLocalDirection.z -= 1.0f;
    }
    if (keysDown_.contains(ViewportKey::D)) {
        input.flyLocalDirection.x += 1.0f;
    }
    if (keysDown_.contains(ViewportKey::A)) {
        input.flyLocalDirection.x -= 1.0f;
    }
    input.pressedKeys = pressedKeys_;
    return input;
}

ViewportModifiers ViewportHost::currentModifiers() const {
    const SDL_Keymod mod = SDL_GetModState();
    return {
            .primary = (mod & SDL_KMOD_CTRL) != 0 || (mod & SDL_KMOD_GUI) != 0,
            .shift = (mod & SDL_KMOD_SHIFT) != 0,
            .alt = (mod & SDL_KMOD_ALT) != 0,
    };
}

void ViewportHost::clearTransientInput() {
    middlePressed_ = false;
    middleReleased_ = false;
    rightPressed_ = false;
    rightReleased_ = false;
    wheelY_ = 0.0f;
    mouseDeltaWindow_ = {};
    pressedKeys_.clear();
}

void ViewportHost::spawnWindowedViewport() {
    if (!renderSystem_) {
        LOG_WARN("Cannot spawn a windowed viewport before the render system is available");
        return;
    }

    const std::size_t totalViewportCount = 1u + windowedViewports_.size();
    if (totalViewportCount >= kMaxViewportComponents) {
        if (!windowedViewports_.empty() && !SDL_RaiseWindow(windowedViewports_.back()->window.sdlWindow())) {
            LOG_WARN("Unable to raise latest windowed viewport: {}", SDL_GetError());
        }
        LOG_INFO("Viewport component limit reached: {}", kMaxViewportComponents);
        return;
    }

    const std::uint8_t layerIndex = nextWindowedViewportLayerIndex();
    if (layerIndex == 0u || layerIndex >= kMaxViewportComponents) {
        LOG_WARN("Unable to allocate a viewport layer for a new windowed viewport");
        return;
    }

    auto instance = std::make_unique<ViewportWindowInstance>();
    instance->component = windowedViewportComponent(layerIndex);
    const std::string title = "Quader Viewport " + std::to_string(static_cast<int>(layerIndex) + 1);
    if (!instance->window.create(title.c_str(), 960, 540)) {
        return;
    }

    instance->windowId = SDL_GetWindowID(instance->window.sdlWindow());
    instance->swapChain = renderSystem_->createAdditionalSwapChain(
            instance->window.nativeWindowHandle(),
            SwapChainPresentationColorSpace::Srgb);
    if (!instance->swapChain) {
        LOG_WARN("Unable to create windowed viewport swapchain");
        instance->window.destroy();
        return;
    }

    instance->window.refreshPixelSize();
    instance->framebufferWidth = instance->window.widthPixels();
    instance->framebufferHeight = instance->window.heightPixels();
    SDL_GetWindowSize(instance->window.sdlWindow(), &instance->windowWidth, &instance->windowHeight);
    instance->windowWidth = std::max(1, instance->windowWidth);
    instance->windowHeight = std::max(1, instance->windowHeight);
    instance->camera.focusOrigin(64.0f);
    instance->inputRouter.setGridPreset(inputRouter_.state().gridPreset);
    instance->renderer.initialize(
            renderSystem_->engine(),
            renderSystem_->scene(),
            renderSystem_->textures(),
            renderSystem_->groundGridMaterial(),
            viewportGridLayerMask(instance->component.layerIndex),
            ViewportRenderSurface::Swapchain);
    instance->renderer.resize(instance->framebufferWidth, instance->framebufferHeight);
    LOG_INFO("Spawned windowed viewport {}", instance->component.stableId);
    windowedViewports_.push_back(std::move(instance));
}

void ViewportHost::shutdownWindowedViewports() {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        shutdownWindowedViewport(*viewport);
    }
    windowedViewports_.clear();
}

bool ViewportHost::processWindowedViewportEvent(const SDL_Event& event) {
    const std::optional<SDL_WindowID> windowId = eventWindowId(event);
    if (!windowId) {
        return false;
    }

    const auto found = std::find_if(windowedViewports_.begin(), windowedViewports_.end(), [&](const auto& viewport) {
        return viewport->windowId == *windowId;
    });
    if (found == windowedViewports_.end()) {
        return false;
    }

    ViewportWindowInstance& instance = **found;
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        shutdownWindowedViewport(instance);
        windowedViewports_.erase(found);
        return true;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
        instance.window.refreshPixelSize();
        instance.framebufferWidth = instance.window.widthPixels();
        instance.framebufferHeight = instance.window.heightPixels();
        SDL_GetWindowSize(instance.window.sdlWindow(), &instance.windowWidth, &instance.windowHeight);
        instance.windowWidth = std::max(1, instance.windowWidth);
        instance.windowHeight = std::max(1, instance.windowHeight);
        return true;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        instance.mouseWindow = {-1.0f, -1.0f};
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        instance.mouseWindow = {event.motion.x, event.motion.y};
        instance.mouseDeltaWindow = instance.mouseDeltaWindow + QVec2{event.motion.xrel, event.motion.yrel};
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        instance.mouseWindow = {event.button.x, event.button.y};
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            instance.middleDown = down;
            instance.middlePressed = instance.middlePressed || down;
            instance.middleReleased = instance.middleReleased || !down;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            instance.rightDown = down;
            instance.rightPressed = instance.rightPressed || down;
            instance.rightReleased = instance.rightReleased || !down;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        instance.mouseWindow = {event.wheel.mouse_x, event.wheel.mouse_y};
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        instance.wheelY += event.wheel.y * direction;
        return true;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const std::optional<ViewportKey> key = viewportKeyFromSdlKey(event.key.key);
        if (key) {
            if (event.key.down) {
                instance.keysDown.insert(*key);
                if (!event.key.repeat) {
                    instance.pressedKeys.push_back(*key);
                }
            } else {
                instance.keysDown.erase(*key);
            }
        }
        return true;
    }
    default:
        return true;
    }
}

void ViewportHost::handleWindowedViewportFrames(
        ViewportWorkspaceService& workspace,
        SceneDocument& document,
        const AssetRegistry& assets) {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        instance.inputRouter.setGridPreset(workspace.gridPreset());
        const ViewportInputResult result =
                instance.inputRouter.handleFrame(buildWindowedViewportInputFrame(instance), instance.camera);
        if (result.gridPresetChanged) {
            workspace.setGridPreset(instance.inputRouter.state().gridPreset);
        }
        if (result.focusDocumentRequested) {
            focusDocument(document, assets, instance.camera);
        }
        setWindowedViewportCursorCaptured(instance, result.flyCursorCaptureActive);
        clearWindowedViewportTransientInput(instance);
    }
}

void ViewportHost::prepareWindowedViewports(ViewportWorkspaceService& workspace) {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        instance.window.refreshPixelSize();
        instance.framebufferWidth = instance.window.widthPixels();
        instance.framebufferHeight = instance.window.heightPixels();
        instance.renderer.resize(instance.framebufferWidth, instance.framebufferHeight);
        instance.renderer.updateCamera(instance.camera);
        instance.renderer.updateGroundGrid(
                instance.camera,
                viewportGridWorldSizeForPreset(workspace.gridPreset()),
                settings_,
                true);
        instance.renderer.setSceneVisible(true);
        instance.renderer.setOverlayVisible(false);
    }
}

ViewportInputFrame ViewportHost::buildWindowedViewportInputFrame(const ViewportWindowInstance& instance) const {
    const float scaleX = instance.windowWidth > 0
            ? static_cast<float>(instance.framebufferWidth) / static_cast<float>(instance.windowWidth)
            : 1.0f;
    const float scaleY = instance.windowHeight > 0
            ? static_cast<float>(instance.framebufferHeight) / static_cast<float>(instance.windowHeight)
            : 1.0f;
    const QVec2 mousePixel = {
            instance.mouseWindow.x * scaleX,
            instance.mouseWindow.y * scaleY,
    };
    const QVec2 mouseDelta = instance.relativeMouseCaptured
            ? instance.relativeMouseDelta
            : QVec2{instance.mouseDeltaWindow.x * scaleX, instance.mouseDeltaWindow.y * scaleY};
    const bool hovered = mousePixel.x >= 0.0f &&
            mousePixel.y >= 0.0f &&
            mousePixel.x <= static_cast<float>(instance.framebufferWidth) &&
            mousePixel.y <= static_cast<float>(instance.framebufferHeight);
    ViewportInputFrame input;
    input.viewportHovered = hovered || instance.inputRouter.navigating();
    input.viewportFocused = true;
    input.textInputActive = false;
    input.middlePressed = instance.middlePressed && input.viewportHovered;
    input.middleDown = instance.middleDown;
    input.middleReleased = instance.middleReleased;
    input.rightPressed = instance.rightPressed && input.viewportHovered;
    input.rightDown = instance.rightDown;
    input.rightReleased = instance.rightReleased;
    input.mouseWheel = input.viewportHovered ? instance.wheelY : 0.0f;
    input.deltaSeconds = static_cast<float>(std::max(instance.frameTiming.deltaSeconds, 0.0));
    input.mouseScreen = mousePixel;
    input.mouseLocal = mousePixel;
    input.mouseDelta = mouseDelta;
    input.viewportSize = {
            static_cast<float>(instance.framebufferWidth),
            static_cast<float>(instance.framebufferHeight),
    };
    input.modifiers = currentModifiers();
    if (instance.keysDown.contains(ViewportKey::W)) {
        input.flyLocalDirection.z += 1.0f;
    }
    if (instance.keysDown.contains(ViewportKey::S)) {
        input.flyLocalDirection.z -= 1.0f;
    }
    if (instance.keysDown.contains(ViewportKey::D)) {
        input.flyLocalDirection.x += 1.0f;
    }
    if (instance.keysDown.contains(ViewportKey::A)) {
        input.flyLocalDirection.x -= 1.0f;
    }
    input.pressedKeys = instance.pressedKeys;
    return input;
}

void ViewportHost::setWindowedViewportCursorCaptured(ViewportWindowInstance& instance, bool captured) {
    if (!instance.window.sdlWindow() || instance.relativeMouseCaptured == captured) {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(instance.window.sdlWindow(), captured)) {
        if (!instance.relativeMouseCaptureWarningIssued) {
            LOG_WARN("Unable to {} windowed viewport relative mouse mode: {}",
                    captured ? "enable" : "disable",
                    SDL_GetError());
            instance.relativeMouseCaptureWarningIssued = true;
        }
        return;
    }

    instance.relativeMouseCaptured = captured;
    instance.relativeMouseCaptureWarningIssued = false;
    float discardX = 0.0f;
    float discardY = 0.0f;
    (void)SDL_GetRelativeMouseState(&discardX, &discardY);
    instance.relativeMouseDelta = {};
}

void ViewportHost::clearWindowedViewportTransientInput(ViewportWindowInstance& instance) {
    instance.middlePressed = false;
    instance.middleReleased = false;
    instance.rightPressed = false;
    instance.rightReleased = false;
    instance.wheelY = 0.0f;
    instance.mouseDeltaWindow = {};
    instance.pressedKeys.clear();
}

std::uint8_t ViewportHost::nextWindowedViewportLayerIndex() const {
    for (std::uint8_t layerIndex = 1u; layerIndex < kMaxViewportComponents; ++layerIndex) {
        const bool used = std::any_of(windowedViewports_.begin(), windowedViewports_.end(), [layerIndex](const auto& viewport) {
            return viewport->component.layerIndex == layerIndex;
        });
        if (!used) {
            return layerIndex;
        }
    }
    return 0u;
}

void ViewportHost::shutdownWindowedViewport(ViewportWindowInstance& instance) {
    setWindowedViewportCursorCaptured(instance, false);
    instance.renderer.shutdown();
    if (renderSystem_ && instance.swapChain) {
        renderSystem_->destroyAdditionalSwapChain(instance.swapChain);
        instance.swapChain = nullptr;
    }
    instance.window.destroy();
}

} // namespace quader

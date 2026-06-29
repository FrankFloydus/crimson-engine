////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app.hpp"

#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "assets/material_document.hpp"
#include "core/logger.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "platform/sdl_event_utils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace quader {

App::App() = default;
App::~App() {
    shutdown();
}

bool App::initialize(const std::filesystem::path& executablePath, AppOptions options) {
    shutdownComplete_ = false;
    running_ = false;
    const std::filesystem::path executableDir = executablePath.parent_path();
    windowSettingsPath_ = AppWindowSettings::resolvePath(executableDir);
    windowSettings_ = AppWindowSettings::load(windowSettingsPath_);
    const std::optional<PlatformWindowPlacement> savedPlacement = windowSettings_.placement();
    const PlatformWindowChromeFrame initialFrame = savedPlacement ?
            savedPlacement->normalFrame :
            PlatformWindowChromeFrame{0, 0, 1600, 960};

    if (!window_.create(
                "Quader",
                std::max(kPlatformWindowMinimumWidth, initialFrame.width),
                std::max(kPlatformWindowMinimumHeight, initialFrame.height),
                {.borderless = options.borderlessWindow})) {
        return false;
    }
    if (savedPlacement) {
        window_.applyPlacement(*savedPlacement);
    }

    if (!renderSystem_.initialize(window_.nativeWindowHandle(), executableDir, RenderConfig{.backend = RendererBackend::Vulkan})) {
        LOG_ERROR("Renderer initialization failed");
        shutdown();
        return false;
    }

    LOG_INFO("Initializing scene render bridge");
    renderBridge_.initialize(renderSystem_.engine(),
            renderSystem_.scene(),
            renderSystem_.quaderMeshMaterial(),
            renderSystem_.vertexColorOverlayMaterial(),
            renderSystem_.overlayLineMaterial(),
            renderSystem_.overlayPointMaterial(),
            executableDir);
    LOG_INFO("Scene render bridge initialized");

    LOG_INFO("Initializing UI system");
    ui_.initialize(window_.sdlWindow(), renderSystem_, executableDir, options.borderlessWindow);
    LOG_INFO("UI system initialized");

    createStartupScene();
    editor_ = std::make_unique<QuaderEditorApi>(
            QuaderApiSessionInternalAccess::fromProjectSession(projectSession_));
    window_.setLiveResizeFrameCallback([this]() {
        if (!running_ || shutdownComplete_ || renderingFrame_) {
            return;
        }
        liveResizeFrameRendered_ = renderFrame(frameClock_.tick());
    });

    if (SDL_AddEventWatch(&App::liveResizeEventWatch, this)) {
        eventWatchInstalled_ = true;
    } else {
        LOG_WARN("SDL_AddEventWatch failed for live resize redraw: {}", SDL_GetError());
    }

    running_ = true;
    return true;
}

int App::run() {
    LOG_INFO("Entering main loop");
    frameClock_.reset();
    const SDL_WindowID mainWindowId = SDL_GetWindowID(window_.sdlWindow());

    while (running_) {
        const FrameTiming frameTiming = frameClock_.tick();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const bool chromeConsumed = window_.processChromeEvent(event);
            if (!chromeConsumed) {
                ui_.processEvent(event);
                if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    (void)window_.processChromeEvent(event);
                }
            }
            if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && sdlEventTargetsWindow(event, mainWindowId))) {
                running_ = false;
            }
            if ((event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                        event.type == SDL_EVENT_WINDOW_RESIZED) &&
                    sdlEventTargetsWindow(event, mainWindowId)) {
                window_.refreshPixelSize();
            }
            if ((event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                        event.type == SDL_EVENT_WINDOW_FOCUS_LOST) &&
                    sdlEventTargetsWindow(event, mainWindowId)) {
                window_.refreshNativeChrome();
            }
        }
        if (!running_) {
            break;
        }

        if (liveResizeFrameRendered_) {
            liveResizeFrameRendered_ = false;
        } else {
            renderFrame(frameTiming);
        }

        SDL_Delay(1);
    }

    return 0;
}

void App::shutdown() {
    if (shutdownComplete_) {
        return;
    }

    shutdownComplete_ = true;
    running_ = false;
    if (eventWatchInstalled_) {
        SDL_RemoveEventWatch(&App::liveResizeEventWatch, this);
        eventWatchInstalled_ = false;
    }
    window_.setLiveResizeFrameCallback({});
    saveWindowSettings();
    ui_.shutdown();
    editor_.reset();
    renderBridge_.shutdown();
    renderSystem_.shutdown();
    window_.destroy();
}

void App::createStartupScene() {
    TextureAsset albedo;
    albedo.name = "Default Albedo";
    albedo.usage = TextureUsage::BaseColor;
    albedo.colorSpace = TextureColorSpace::Srgb;
    albedo.sourcePath = "materials/default/default_albedo.png";
    const AssetId albedoTexture = projectSession_.assets().addTexture(std::move(albedo));

    TextureAsset roughness;
    roughness.name = "Default Roughness";
    roughness.usage = TextureUsage::Roughness;
    roughness.colorSpace = TextureColorSpace::Linear;
    roughness.sourcePath = "materials/default/default_roughness.png";
    const AssetId roughnessTexture = projectSession_.assets().addTexture(std::move(roughness));

    MaterialDocument defaultMaterial = makeDefaultQuaderMaterial("Default Quader", albedoTexture, roughnessTexture);
    defaultMaterial_ = projectSession_.assets().addMaterial(std::move(defaultMaterial));
}

void App::handleWindowChromeAction(WindowChromeAction action) {
    switch (action) {
    case WindowChromeAction::Minimize:
        window_.minimize();
        break;
    case WindowChromeAction::ToggleMaximizeRestore:
        window_.toggleMaximizeRestore();
        window_.refreshNativeChrome();
        break;
    case WindowChromeAction::Close:
        running_ = false;
        break;
    }
}

void App::saveWindowSettings() {
    if (windowSettingsPath_.empty()) {
        return;
    }

    const std::optional<PlatformWindowPlacement> placement = window_.placement();
    if (!placement) {
        return;
    }

    windowSettings_.setPlacement(*placement);
    if (!windowSettings_.save(windowSettingsPath_)) {
        LOG_WARN("Unable to save window settings to {}", windowSettingsPath_.string());
    }
}

bool App::renderFrame(const FrameTiming& frameTiming) {
    if (renderingFrame_ || !editor_) {
        return running_;
    }

    renderingFrame_ = true;

    window_.refreshPixelSize();
    ui_.setViewportRenderResizeSuspended(window_.chromeResizeActive());
    ui_.beginFrame(frameTiming);
    bool quitRequested = false;
    ui_.draw(*editor_,
            renderBridge_,
            projectSession_.scene(),
            projectSession_.assets(),
            projectSession_.modeling(),
            projectSession_.selection(),
            defaultMaterial_,
            quitRequested);
    if (const std::optional<WindowChromeAction> chromeAction = ui_.takeWindowChromeAction()) {
        handleWindowChromeAction(*chromeAction);
    }
    if (quitRequested) {
        running_ = false;
    }
    if (!running_) {
        renderingFrame_ = false;
        return false;
    }
    (void)QuaderRenderApiRuntimeAccess::syncScene(editor_->render(), renderBridge_);

    ui_.prepareViewports(*editor_, renderBridge_, projectSession_.scene(), projectSession_.assets());
    ui_.prepareRenderData(window_.widthPixels(), window_.heightPixels());

    if (renderSystem_.beginFrame()) {
        ui_.renderViewports(renderSystem_);
        ui_.render(renderSystem_);
        renderSystem_.endFrame();
    }

    renderingFrame_ = false;
    return running_;
}

bool App::shouldRenderLiveResizeEvent(const SDL_Event& event) const {
    if (!running_ || shutdownComplete_ || renderingFrame_ || event.type != SDL_EVENT_WINDOW_EXPOSED ||
            event.window.data1 != 1 || window_.sdlWindow() == nullptr) {
        return false;
    }

    return sdlEventTargetsWindow(event, SDL_GetWindowID(window_.sdlWindow()));
}

bool App::liveResizeEventWatch(void* userdata, SDL_Event* event) {
    if (userdata == nullptr || event == nullptr || event->type != SDL_EVENT_WINDOW_EXPOSED) {
        return true;
    }

    App* app = static_cast<App*>(userdata);
    if (!app->shouldRenderLiveResizeEvent(*event)) {
        return true;
    }

    app->liveResizeFrameRendered_ = app->renderFrame(app->frameClock_.tick());
    return true;
}

} // namespace quader

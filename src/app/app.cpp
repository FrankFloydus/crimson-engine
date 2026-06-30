////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app.hpp"

#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "assets/material_document.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "platform/platform_file_dialog.hpp"
#include "platform/sdl_event_utils.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace quader {
namespace {

constexpr Sint32 kActiveFrameWaitMs = 1;
constexpr Sint32 kIdleFrameWaitMs = 100;
constexpr Sint32 kSuspendedWindowWaitMs = 250;

std::filesystem::path defaultProjectSaveFileName(const QuaderProjectApi& project) {
    std::string name = project.metadata().name.empty() ? "Untitled Project" : project.metadata().name;
    name.append(".qdr");
    return std::filesystem::path(std::move(name));
}

PlatformFileDialogOptions projectFileDialogOptions(void* ownerWindowHandle, const QuaderProjectApi& project) {
    PlatformFileDialogOptions options;
    options.ownerWindowHandle = ownerWindowHandle;
    const std::filesystem::path& currentPath = project.path();
    if (!currentPath.empty()) {
        options.initialDirectory = currentPath.parent_path();
        options.initialFileName = currentPath.filename();
    } else {
        options.initialFileName = defaultProjectSaveFileName(project);
    }
    return options;
}

} // namespace

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
            renderSystem_.quaderMeshLitMaterial(),
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
    editor_->project().setRuntimeHooks(QuaderProjectRuntimeHooks{
            .chooseOpenPath = [this]() {
                return platformOpenProjectFileDialog(
                        projectFileDialogOptions(window_.nativeWindowHandle(), editor_->project()));
            },
            .chooseSaveAsPath = [this]() {
                return platformSaveProjectFileDialog(
                        projectFileDialogOptions(window_.nativeWindowHandle(), editor_->project()));
            },
    });
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
                        event.type == SDL_EVENT_WINDOW_RESIZED ||
                        event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                        event.type == SDL_EVENT_WINDOW_RESTORED ||
                        event.type == SDL_EVENT_WINDOW_SHOWN ||
                        event.type == SDL_EVENT_WINDOW_EXPOSED) &&
                    sdlEventTargetsWindow(event, mainWindowId)) {
                window_.refreshPixelSize();
            }
            if ((event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                        event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                        event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                        event.type == SDL_EVENT_WINDOW_RESTORED ||
                        event.type == SDL_EVENT_WINDOW_SHOWN ||
                        event.type == SDL_EVENT_WINDOW_HIDDEN ||
                        event.type == SDL_EVENT_WINDOW_MINIMIZED ||
                        event.type == SDL_EVENT_WINDOW_OCCLUDED) &&
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
            if (window_.canRenderFrames()) {
                renderFrame(frameTiming);
            }
        }

        waitUntilNextFrame();
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
    albedo.name = std::string(kDefaultMaterialAlbedoTextureName);
    albedo.usage = TextureUsage::BaseColor;
    albedo.colorSpace = TextureColorSpace::Srgb;
    albedo.sourcePath = std::string(kDefaultMaterialAlbedoTexturePackagePath);
    const AssetId albedoTexture = projectSession_.assets().addTexture(std::move(albedo));

    TextureAsset roughness;
    roughness.name = std::string(kDefaultMaterialRoughnessTextureName);
    roughness.usage = TextureUsage::Roughness;
    roughness.colorSpace = TextureColorSpace::Linear;
    roughness.sourcePath = std::string(kDefaultMaterialRoughnessTexturePackagePath);
    projectSession_.assets().addTexture(std::move(roughness));

    MaterialDocument defaultMaterial = makeDefaultQuaderMaterial("Default Quader");
    setMaterialTextureSlot(defaultMaterial, MaterialTextureSlot::BaseColor, albedoTexture);
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
    QDR_PROFILE_FUNCTION();
    if (renderingFrame_ || !editor_) {
        return running_;
    }

    renderingFrame_ = true;

    window_.refreshPixelSize();
    if (!window_.canRenderFrames()) {
        renderingFrame_ = false;
        return running_;
    }
    ui_.setViewportRenderResizeSuspended(window_.chromeResizeActive());
    ui_.setRenderPerformanceStats(renderSystem_.framePerformanceStats());
    {
        QDR_PROFILE_SCOPE("UI begin frame");
        ui_.beginFrame(frameTiming);
    }
    bool quitRequested = false;
    {
        QDR_PROFILE_SCOPE("UI draw");
        ui_.draw(*editor_,
                renderBridge_,
                projectSession_.scene(),
                projectSession_.assets(),
                projectSession_.modeling(),
                projectSession_.selection(),
                defaultMaterial_,
                quitRequested);
    }
    if (const std::optional<WindowChromeAction> chromeAction = ui_.takeWindowChromeAction()) {
        handleWindowChromeAction(*chromeAction);
    }
    if (quitRequested) {
        running_ = false;
    }
    if (!running_) {
        ui_.endFrameWithoutRender();
        renderingFrame_ = false;
        return false;
    }
    if (!window_.canRenderFrames()) {
        ui_.endFrameWithoutRender();
        renderingFrame_ = false;
        return running_;
    }
    {
        QDR_PROFILE_SCOPE("Render sync scene");
        (void)QuaderRenderApiRuntimeAccess::syncScene(editor_->render(), renderBridge_);
    }
    if (const EntityId root = projectSession_.scene().sceneRoot(); root != InvalidEntityId) {
        if (const SceneSettingsComponent* settings = projectSession_.scene().sceneSettings(root)) {
            renderSystem_.applySceneSettings(*settings);
        }
    }

    {
        QDR_PROFILE_SCOPE("UI prepare viewports");
        ui_.prepareViewports(*editor_, renderBridge_, projectSession_.scene(), projectSession_.assets());
    }
    {
        QDR_PROFILE_SCOPE("UI prepare render data");
        ui_.prepareRenderData(window_.widthPixels(), window_.heightPixels());
    }

    {
        QDR_PROFILE_SCOPE("Render frame submit");
        if (renderSystem_.beginFrame()) {
            {
                QDR_PROFILE_SCOPE("Render viewport panes");
                ui_.renderViewports(renderSystem_);
            }
            {
                QDR_PROFILE_SCOPE("Render ImGui");
                ui_.render(renderSystem_);
            }
            renderSystem_.endFrame();
        }
    }

    renderingFrame_ = false;
    QDR_PROFILE_FRAME_MARK();
    return running_;
}

void App::waitUntilNextFrame() {
    const bool drawable = window_.canRenderFrames();
    const Sint32 waitMs = !drawable ? kSuspendedWindowWaitMs :
            (ui_.needsContinuousRendering() || window_.chromeResizeActive() ? kActiveFrameWaitMs : kIdleFrameWaitMs);
    (void)SDL_WaitEventTimeout(nullptr, waitMs);
}

bool App::shouldRenderLiveResizeEvent(const SDL_Event& event) const {
    if (!running_ || shutdownComplete_ || renderingFrame_ || event.type != SDL_EVENT_WINDOW_EXPOSED ||
            event.window.data1 != 1 || window_.sdlWindow() == nullptr || !window_.canRenderFrames()) {
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

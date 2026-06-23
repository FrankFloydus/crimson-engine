////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/App.h"

#include "api/QuaderEditorApi.h"
#include "assets/MaterialDocument.h"
#include "core/Logger.h"

#include <SDL3/SDL.h>

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

    if (!window_.create("Quader", 1600, 960, {.borderless = options.borderlessWindow})) {
        return false;
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
    ui_.initialize(window_.sdlWindow(), renderSystem_, executableDir);
    LOG_INFO("UI system initialized");

    createStartupScene();
    editor_ = std::make_unique<QuaderEditorApi>(projectSession_);

    running_ = true;
    return true;
}

int App::run() {
    LOG_INFO("Entering main loop");
    frameClock_.reset();

    while (running_) {
        const FrameTiming frameTiming = frameClock_.tick();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ui_.processEvent(event);
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running_ = false;
            }
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                    event.type == SDL_EVENT_WINDOW_RESIZED) {
                window_.refreshPixelSize();
            }
        }
        if (!running_) {
            break;
        }

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
        if (quitRequested) {
            running_ = false;
        }
        (void)editor_->render().syncScene(renderBridge_);

        window_.refreshPixelSize();
        ui_.prepareRenderData(window_.widthPixels(), window_.heightPixels());
        ui_.prepareViewports(*editor_, renderBridge_, projectSession_.scene(), projectSession_.assets());

        if (renderSystem_.beginFrame()) {
            ui_.renderViewports(renderSystem_);
            ui_.render(renderSystem_);
            renderSystem_.endFrame();
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

} // namespace quader

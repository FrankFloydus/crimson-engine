#include "quader/app/App.h"

#include "quader/assets/MaterialDocument.h"
#include "quader/core/Logger.h"

#include <SDL3/SDL.h>

namespace quader {

bool App::initialize(const std::filesystem::path& executablePath, AppOptions options) {
    const std::filesystem::path executableDir = executablePath.parent_path();

    if (!window_.create("Quader", 1600, 960, {.borderless = options.borderlessWindow})) {
        return false;
    }

    if (!renderSystem_.initialize(window_.nativeWindowHandle(), executableDir, RenderConfig{.backend = RendererBackend::Vulkan})) {
        LOG_ERROR("Renderer initialization failed");
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

    undoRedo_ = std::make_unique<UndoRedoManager>();
    documentCommands_ = std::make_unique<DocumentCommandService>(document_, *undoRedo_);
    modeling_ = std::make_unique<ModelingSessionService>(document_, assets_);
    createStartupScene();

    running_ = true;
    return true;
}

int App::run() {
    LOG_INFO("Entering main loop");

    while (running_) {
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

        ui_.beginFrame();
        bool quitRequested = false;
        ui_.draw(renderBridge_,
                document_,
                assets_,
                *undoRedo_,
                *documentCommands_,
                *modeling_,
                defaultMaterial_,
                quitRequested);
        if (quitRequested) {
            running_ = false;
        }
        renderBridge_.sync(document_, assets_);

        window_.refreshPixelSize();
        ui_.prepareRenderData(window_.widthPixels(), window_.heightPixels());
        ui_.prepareViewports(renderBridge_);

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
    ui_.shutdown();
    renderBridge_.shutdown();
    renderSystem_.shutdown();
    modeling_.reset();
    documentCommands_.reset();
    undoRedo_.reset();
    window_.destroy();
}

void App::createStartupScene() {
    const AssetId albedoTexture = assets_.addTexture(TextureAsset{
            .name = "Default Albedo",
            .usage = TextureUsage::BaseColor,
            .colorSpace = TextureColorSpace::Srgb,
            .sourcePath = "materials/default/default_albedo.png",
    });
    const AssetId roughnessTexture = assets_.addTexture(TextureAsset{
            .name = "Default Roughness",
            .usage = TextureUsage::Roughness,
            .colorSpace = TextureColorSpace::Linear,
            .sourcePath = "materials/default/default_roughness.png",
    });
    MaterialDocument defaultMaterial = makeDefaultQuaderMaterial("Default Quader", albedoTexture, roughnessTexture);
    defaultMaterial_ = assets_.addMaterial(std::move(defaultMaterial));
}

} // namespace quader

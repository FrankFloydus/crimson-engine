#include "quader/app/App.h"

#include "quader/assets/MaterialDocument.h"
#include "quader/core/Logger.h"

#include <SDL3/SDL.h>

namespace quader {

bool App::initialize(const std::filesystem::path& executablePath) {
    const std::filesystem::path executableDir = executablePath.parent_path();

    if (!window_.create("Quader", 1600, 960)) {
        return false;
    }

    if (!renderSystem_.initialize(window_.nativeWindowHandle(), executableDir, RenderConfig{.backend = RendererBackend::Vulkan})) {
        LOG_ERROR("Renderer initialization failed");
        return false;
    }

    LOG_INFO("Initializing viewport renderer");
    viewportRenderer_.initialize(renderSystem_.engine(),
            renderSystem_.scene(),
            renderSystem_.textures(),
            renderSystem_.groundGridMaterial());
    LOG_INFO("Viewport renderer initialized");

    LOG_INFO("Initializing scene render bridge");
    renderBridge_.initialize(renderSystem_.engine(),
            renderSystem_.scene(),
            renderSystem_.quaderMeshMaterial(),
            renderSystem_.vertexColorOverlayMaterial(),
            executableDir);
    LOG_INFO("Scene render bridge initialized");

    LOG_INFO("Initializing UI system");
    ui_.initialize(window_.sdlWindow(), renderSystem_, executableDir);
    LOG_INFO("UI system initialized");

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
        ui_.draw(viewportRenderer_, viewportCamera_, renderBridge_, document_, assets_, *modeling_, defaultMaterial_, quitRequested);
        if (quitRequested) {
            running_ = false;
        }
        viewportRenderer_.updateCamera(viewportCamera_);
        const float gridWorldSize = viewportGridWorldSizeForPreset(ui_.viewportToolState().gridPreset);
        viewportRenderer_.updateGroundGrid(viewportCamera_, gridWorldSize);
        renderBridge_.sync(document_, assets_);

        window_.refreshPixelSize();
        ui_.prepareRenderData(window_.widthPixels(), window_.heightPixels());

        if (renderSystem_.beginFrame()) {
            viewportRenderer_.render(renderSystem_.renderer());
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
    viewportRenderer_.shutdown();
    renderSystem_.shutdown();
    modeling_.reset();
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

    if (modeling_ && modeling_->sdkAvailable()) {
        const ModelingCreateBoxResult box = modeling_->createBoxFromBounds({
                .bounds = {
                        .min = {-1.0f, -1.0f, -1.0f},
                        .max = {1.0f, 1.0f, 1.0f},
                },
                .name = "Box",
        });
        if (box.ok) {
            document_.setMaterial(box.object.entity, MaterialComponent{.material = defaultMaterial_});
            LOG_INFO("Startup scene created SDK box entity {} with mesh asset {}", box.object.entity, box.object.mesh);
        } else {
            LOG_ERROR("Unable to create startup SDK box: {}", box.message);
        }
    } else {
        LOG_ERROR("Modeling SDK is unavailable; startup box was not created");
    }

    const EntityId lightEntity = document_.createEntity("Fake Directional Light");
    document_.setTransform(lightEntity, TransformComponent{.rotationEuler = {-0.65f, -0.55f, 0.0f}});
    document_.setLight(lightEntity, LightComponent{
            .type = LightType::Directional,
            .color = {1.0f, 0.95f, 0.88f},
            .intensity = 90000.0f,
            .castsShadows = false,
    });
}

} // namespace quader

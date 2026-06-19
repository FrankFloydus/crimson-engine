#include "quader/render/RenderSystem.h"

#include "quader/core/Logger.h"
#include "quader/render/MaterialCompilerService.h"

#include <filament/Material.h>
#include <filament/Renderer.h>
#include <filament/Skybox.h>
#include <math/vec4.h>

#include <stdexcept>

namespace quader {
namespace {

filament::backend::Backend toFilamentBackend(RendererBackend backend) {
    switch (backend) {
    case RendererBackend::Vulkan:
        return filament::backend::Backend::VULKAN;
    case RendererBackend::OpenGL:
        return filament::backend::Backend::OPENGL;
    case RendererBackend::Metal:
        return filament::backend::Backend::METAL;
    }
    return filament::backend::Backend::VULKAN;
}

filament::Material* loadMaterial(filament::Engine& engine, const std::filesystem::path& path) {
    const std::vector<std::byte> package = MaterialCompilerService::loadPackage(path);
    return filament::Material::Builder()
            .package(package.data(), package.size())
            .build(engine);
}

} // namespace

RenderSystem::~RenderSystem() {
    shutdown();
}

bool RenderSystem::initialize(void* nativeWindow, const std::filesystem::path& executableDir, RenderConfig config) {
    LOG_INFO("Initializing Filament renderer with {} backend", toString(config.backend));

    engine_ = filament::Engine::create(toFilamentBackend(config.backend));
    if (!engine_) {
        LOG_ERROR("Unable to create Filament engine");
        return false;
    }
    LOG_INFO("Filament engine created");

    swapChain_ = engine_->createSwapChain(nativeWindow);
    LOG_INFO("Filament swapchain created for native window {}", nativeWindow);
    renderer_ = engine_->createRenderer();
    LOG_INFO("Filament renderer created");
    scene_ = engine_->createScene();
    LOG_INFO("Filament scene created");
    skybox_ = filament::Skybox::Builder()
            .color({0.051f, 0.051f, 0.051f, 1.0f})
            .build(*engine_);
    scene_->setSkybox(skybox_);
    LOG_INFO("Filament skybox created");

    filament::Renderer::ClearOptions clear;
    clear.clear = true;
    clear.discard = true;
    clear.clearColor = {0.051, 0.051, 0.051, 1.0};
    renderer_->setClearOptions(clear);

    try {
        LOG_INFO("Loading Quader mesh material");
        quaderMeshMaterial_ = loadMaterial(*engine_, executableDir / "materials" / "quader_mesh.filamat");
        LOG_INFO("Loading vertex color overlay material");
        vertexColorOverlayMaterial_ = loadMaterial(*engine_, executableDir / "materials" / "vertex_color_overlay.filamat");
        LOG_INFO("Loading ImGui material");
        imguiMaterial_ = loadMaterial(*engine_, executableDir / "materials" / "imgui.filamat");
        LOG_INFO("Loading ground grid material");
        groundGridMaterial_ = loadMaterial(*engine_, executableDir / "materials" / "ground_grid.filamat");
    } catch (const std::exception& error) {
        LOG_ERROR("Material load failed: {}", error.what());
        return false;
    }
    LOG_INFO("Filament materials loaded");

    return true;
}

void RenderSystem::shutdown() {
    if (!engine_) {
        return;
    }

    textureRegistry_.clear();

    if (quaderMeshMaterial_) {
        engine_->destroy(quaderMeshMaterial_);
        quaderMeshMaterial_ = nullptr;
    }
    if (vertexColorOverlayMaterial_) {
        engine_->destroy(vertexColorOverlayMaterial_);
        vertexColorOverlayMaterial_ = nullptr;
    }
    if (imguiMaterial_) {
        engine_->destroy(imguiMaterial_);
        imguiMaterial_ = nullptr;
    }
    if (groundGridMaterial_) {
        engine_->destroy(groundGridMaterial_);
        groundGridMaterial_ = nullptr;
    }
    if (skybox_) {
        scene_->setSkybox(nullptr);
        engine_->destroy(skybox_);
        skybox_ = nullptr;
    }
    if (scene_) {
        engine_->destroy(scene_);
        scene_ = nullptr;
    }
    if (renderer_) {
        engine_->destroy(renderer_);
        renderer_ = nullptr;
    }
    if (swapChain_) {
        engine_->destroy(swapChain_);
        swapChain_ = nullptr;
    }

    filament::Engine::destroy(&engine_);
}

bool RenderSystem::beginFrame() {
    return renderer_->beginFrame(swapChain_);
}

void RenderSystem::endFrame() {
    renderer_->endFrame();
}

} // namespace quader

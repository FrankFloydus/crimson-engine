#pragma once

#include "quader/render/Backend.h"
#include "quader/render/TextureRegistry.h"

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>

#include <filesystem>
#include <memory>

namespace quader {

class RenderSystem {
public:
    ~RenderSystem();

    bool initialize(void* nativeWindow, const std::filesystem::path& executableDir, RenderConfig config = {});
    void shutdown();

    bool beginFrame();
    void endFrame();

    filament::Engine& engine() { return *engine_; }
    filament::Renderer& renderer() { return *renderer_; }
    filament::Scene& scene() { return *scene_; }
    filament::Material& quaderMeshMaterial() { return *quaderMeshMaterial_; }
    filament::Material& vertexColorOverlayMaterial() { return *vertexColorOverlayMaterial_; }
    filament::Material& overlayLineMaterial() { return *overlayLineMaterial_; }
    filament::Material& overlayPointMaterial() { return *overlayPointMaterial_; }
    filament::Material& imguiMaterial() { return *imguiMaterial_; }
    filament::Material& groundGridMaterial() { return *groundGridMaterial_; }
    TextureRegistry& textures() { return textureRegistry_; }

private:
    filament::Engine* engine_ = nullptr;
    filament::Renderer* renderer_ = nullptr;
    filament::SwapChain* swapChain_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Skybox* skybox_ = nullptr;
    filament::Material* quaderMeshMaterial_ = nullptr;
    filament::Material* vertexColorOverlayMaterial_ = nullptr;
    filament::Material* overlayLineMaterial_ = nullptr;
    filament::Material* overlayPointMaterial_ = nullptr;
    filament::Material* imguiMaterial_ = nullptr;
    filament::Material* groundGridMaterial_ = nullptr;
    TextureRegistry textureRegistry_;
};

} // namespace quader

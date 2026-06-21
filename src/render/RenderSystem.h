#pragma once

#include "render/Backend.h"
#include "render/FilamentMaterialLibrary.h"
#include "render/TextureRegistry.h"

#include <filament/Engine.h>
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
    filament::Material& quaderMeshMaterial() { return materialLibrary_.quaderMesh(); }
    filament::Material& vertexColorOverlayMaterial() { return materialLibrary_.vertexColorOverlay(); }
    filament::Material& overlayLineMaterial() { return materialLibrary_.overlayLine(); }
    filament::Material& overlayPointMaterial() { return materialLibrary_.overlayPoint(); }
    filament::Material& imguiMaterial() { return materialLibrary_.imgui(); }
    filament::Material& groundGridMaterial() { return materialLibrary_.groundGrid(); }
    TextureRegistry& textures() { return textureRegistry_; }

private:
    filament::Engine* engine_ = nullptr;
    filament::Renderer* renderer_ = nullptr;
    filament::SwapChain* swapChain_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Skybox* skybox_ = nullptr;
    FilamentMaterialLibrary materialLibrary_;
    TextureRegistry textureRegistry_;
};

} // namespace quader

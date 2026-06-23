////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
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

/**
 * Enumerates SwapChainPresentationColorSpace values used by the render extraction and Filament resource layer.
 */
enum class SwapChainPresentationColorSpace {
    Native,
    Srgb,
};

/**
 * Owns Render System behavior within the render extraction and Filament resource layer.
 */
class RenderSystem {
public:
    ~RenderSystem();

    bool initialize(void* nativeWindow, const std::filesystem::path& executableDir, RenderConfig config = {});
    void shutdown();

    bool beginFrame();
    bool beginFrame(filament::SwapChain* swapChain);
    void endFrame();
    filament::SwapChain* createAdditionalSwapChain(
            void* nativeWindow,
            SwapChainPresentationColorSpace colorSpace = SwapChainPresentationColorSpace::Native);
    void destroyAdditionalSwapChain(filament::SwapChain* swapChain);

    filament::Engine& engine() { return *engine_; }
    filament::Renderer& renderer() { return *renderer_; }
    filament::Scene& scene() { return *scene_; }
    filament::Material& quaderMeshMaterial() { return materialLibrary_.quaderMesh(); }
    filament::Material& vertexColorOverlayMaterial() { return materialLibrary_.vertexColorOverlay(); }
    filament::Material& overlayLineMaterial() { return materialLibrary_.overlayLine(); }
    filament::Material& overlayPointMaterial() { return materialLibrary_.overlayPoint(); }
    filament::Material& rmlUiMaterial() { return materialLibrary_.rmlUi(); }
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

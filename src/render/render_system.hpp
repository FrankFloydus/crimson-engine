////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/backend.hpp"
#include "render/filament_material_library.hpp"
#include "render/texture_registry.hpp"

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

class RenderSystem;

/**
 * Owns one additional Filament swapchain through the RenderSystem that created it.
 */
class RenderAdditionalSwapChain {
public:
    RenderAdditionalSwapChain() = default;
    RenderAdditionalSwapChain(const RenderAdditionalSwapChain&) = delete;
    RenderAdditionalSwapChain& operator=(const RenderAdditionalSwapChain&) = delete;
    RenderAdditionalSwapChain(RenderAdditionalSwapChain&& other) noexcept;
    RenderAdditionalSwapChain& operator=(RenderAdditionalSwapChain&& other) noexcept;
    ~RenderAdditionalSwapChain();

    [[nodiscard]] filament::SwapChain* get() const { return swapChain_; }
    [[nodiscard]] explicit operator bool() const { return swapChain_ != nullptr; }
    void reset();

private:
    friend class RenderSystem;

    RenderAdditionalSwapChain(RenderSystem& owner, filament::SwapChain* swapChain);

    RenderSystem* owner_ = nullptr;
    filament::SwapChain* swapChain_ = nullptr;
};

/**
 * Owns Render System behavior within the render extraction and Filament resource layer.
 */
class RenderSystem {
public:
    RenderSystem() = default;
    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&) = delete;
    RenderSystem& operator=(RenderSystem&&) = delete;
    ~RenderSystem();

    bool initialize(void* nativeWindow, const std::filesystem::path& executableDir, RenderConfig config = {});
    void shutdown();
    [[nodiscard]] bool initialized() const { return engine_ != nullptr; }

    bool beginFrame();
    bool beginFrame(filament::SwapChain* swapChain);
    void endFrame();
    RenderAdditionalSwapChain createAdditionalSwapChain(
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

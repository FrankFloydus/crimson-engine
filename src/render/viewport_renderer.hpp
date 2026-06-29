////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/texture_registry.hpp"
#include "render/viewport_grid_renderer.hpp"
#include "render/viewport_render_target.hpp"
#include "render/viewport_render_pass_coordinator.hpp"
#include "scene/components.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <utils/Entity.h>

#include <cstdint>

namespace quader {

/**
 * Selects whether a viewport renders into a texture or directly into the active swapchain.
 */
enum class ViewportRenderSurface {
    Texture,
    Swapchain,
};

/**
 * Owns Viewport Renderer behavior within the render extraction and Filament resource layer.
 */
class ViewportRenderer {
public:
    ViewportRenderer() = default;
    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;
    ViewportRenderer(ViewportRenderer&&) = delete;
    ViewportRenderer& operator=(ViewportRenderer&&) = delete;
    ~ViewportRenderer();

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            TextureRegistry& textureRegistry,
            filament::Material& groundGridMaterial,
            std::uint8_t gridLayerMask,
            ViewportRenderSurface surface = ViewportRenderSurface::Texture);
    void shutdown();

    void resize(std::uint32_t width, std::uint32_t height);
    void updateCamera(const ViewportCameraController& controller);
    void applySceneSettings(const SceneSettingsComponent& settings, bool renderedMode);
    void updateGroundGrid(const ViewportCameraController& controller,
            float gridWorldSpacing,
            const ViewportSettings& settings,
            bool visible = true);
    void setOverlayVisible(bool visible);
    void setSceneVisible(bool visible);
    void setSceneLayerMask(std::uint8_t layerMask);
    void setShadowingEnabled(bool enabled);
    void render(filament::Renderer& renderer);

    RenderTextureHandle textureHandle() const { return renderTarget_.textureHandle(); }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

private:
    void updateVisibleLayers();
    void applyToneMapping(SceneToneMapping toneMapping, bool enabled);
    void destroyColorGrading();

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    TextureRegistry* textureRegistry_ = nullptr;
    filament::View* view_ = nullptr;
    filament::View* overlayView_ = nullptr;
    filament::Camera* camera_ = nullptr;
    filament::ColorGrading* colorGrading_ = nullptr;
    utils::Entity cameraEntity_;
    ViewportGridRenderer gridRenderer_;
    ViewportRenderPassCoordinator renderPassCoordinator_;
    ViewportRenderTarget renderTarget_;
    ViewportRenderSurface surface_ = ViewportRenderSurface::Texture;
    std::uint32_t width_ = 1u;
    std::uint32_t height_ = 1u;
    float cameraNearPlane_ = 0.05f;
    float cameraFarPlane_ = 1000.0f;
    SceneToneMapping colorGradingToneMapping_ = SceneToneMapping::Aces;
    bool hasColorGradingToneMapping_ = false;
};

} // namespace quader

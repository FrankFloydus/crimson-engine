#pragma once

#include "render/TextureRegistry.h"
#include "render/ViewportGridRenderer.h"
#include "render/ViewportRenderTarget.h"
#include "render/ViewportRenderPassCoordinator.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportSettings.h"

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <imgui.h>
#include <utils/Entity.h>

#include <cstdint>

namespace quader {

class ViewportRenderer {
public:
    ~ViewportRenderer();

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            TextureRegistry& textureRegistry,
            filament::Material& groundGridMaterial,
            std::uint8_t gridLayerMask);
    void shutdown();

    void resize(std::uint32_t width, std::uint32_t height);
    void updateCamera(const ViewportCameraController& controller);
    void updateGroundGrid(const ViewportCameraController& controller,
            float gridWorldSpacing,
            const ViewportSettings& settings,
            bool visible = true);
    void setOverlayVisible(bool visible);
    void setSceneVisible(bool visible);
    void render(filament::Renderer& renderer);

    ImTextureID textureId() const { return renderTarget_.textureId(); }
    std::uint32_t width() const { return renderTarget_.width(); }
    std::uint32_t height() const { return renderTarget_.height(); }

private:
    void updateVisibleLayers();

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    TextureRegistry* textureRegistry_ = nullptr;
    filament::View* view_ = nullptr;
    filament::Camera* camera_ = nullptr;
    utils::Entity cameraEntity_;
    ViewportGridRenderer gridRenderer_;
    ViewportRenderPassCoordinator renderPassCoordinator_;
    ViewportRenderTarget renderTarget_;
};

} // namespace quader

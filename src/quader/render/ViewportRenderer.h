#pragma once

#include "quader/render/TextureRegistry.h"
#include "quader/viewport/ViewportCameraController.h"

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderTarget.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
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
    void updateGroundGrid(const ViewportCameraController& controller, float gridWorldSpacing, bool visible = true);
    void setOverlayVisible(bool visible);
    void render(filament::Renderer& renderer);

    ImTextureID textureId() const { return textureId_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

private:
    void createGroundGrid();
    void updateGroundGridMesh(float width, float height);
    void updateVisibleLayers();
    void destroyGroundGrid();
    void destroyRenderTarget();

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    TextureRegistry* textureRegistry_ = nullptr;
    filament::Material* groundGridMaterial_ = nullptr;
    filament::View* view_ = nullptr;
    filament::Camera* camera_ = nullptr;
    utils::Entity cameraEntity_;
    utils::Entity groundGridEntity_;
    filament::VertexBuffer* groundGridVertexBuffer_ = nullptr;
    filament::IndexBuffer* groundGridIndexBuffer_ = nullptr;
    filament::MaterialInstance* groundGridMaterialInstance_ = nullptr;
    float groundGridMeshWidth_ = 0.0f;
    float groundGridMeshHeight_ = 0.0f;
    bool groundGridVisible_ = true;
    bool overlayVisible_ = false;
    std::uint8_t gridLayerMask_ = 0x04u;
    filament::Texture* colorTexture_ = nullptr;
    filament::Texture* depthTexture_ = nullptr;
    filament::RenderTarget* renderTarget_ = nullptr;
    ImTextureID textureId_ = ImTextureID_Invalid;
    std::uint32_t width_ = 1;
    std::uint32_t height_ = 1;
};

} // namespace quader

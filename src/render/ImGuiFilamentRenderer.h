////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/TextureRegistry.h"

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <imgui.h>
#include <utils/Entity.h>

#include <cstdint>
#include <vector>

namespace quader {

/**
 * Owns Im Gui Filament Renderer behavior within the render extraction and Filament resource layer.
 */
class ImGuiFilamentRenderer {
public:
    ~ImGuiFilamentRenderer();

    void initialize(filament::Engine& engine, filament::Material& material, TextureRegistry& textures);
    void shutdown();
    void releaseFrameResources();
    void prepare(ImDrawData* drawData, std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void render(filament::Renderer& renderer);

private:
    /**
     * Represents a Draw Resource value used by the render extraction and Filament resource layer.
     */
    struct DrawResource {
        utils::Entity entity;
        filament::VertexBuffer* vertexBuffer = nullptr;
        filament::IndexBuffer* indexBuffer = nullptr;
        std::vector<filament::MaterialInstance*> materialInstances;
    };

    void createFontTexture();
    filament::Engine* engine_ = nullptr;
    filament::Material* material_ = nullptr;
    TextureRegistry* textures_ = nullptr;
    filament::Scene* clearScene_ = nullptr;
    filament::View* clearView_ = nullptr;
    filament::Skybox* clearSkybox_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::View* view_ = nullptr;
    filament::Camera* camera_ = nullptr;
    utils::Entity cameraEntity_;
    filament::Texture* fontTexture_ = nullptr;
    ImTextureID fontTextureId_ = ImTextureID_Invalid;
    std::vector<DrawResource> frameResources_;
};

} // namespace quader

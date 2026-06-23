////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/TextureRegistry.h"

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

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
#include <utils/Entity.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {

/**
 * Owns RmlUi geometry submission through Filament.
 */
class RmlFilamentRenderer final : public Rml::RenderInterface {
public:
    ~RmlFilamentRenderer() override;

    void initialize(filament::Engine& engine, filament::Material& material, TextureRegistry& textures);
    void shutdown();

    void beginFrame(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void render(filament::Renderer& renderer);
    void registerTextureSource(
            std::string source,
            RenderTextureHandle textureHandle,
            std::uint32_t width,
            std::uint32_t height);
    void clearTextureSources();

    Rml::CompiledGeometryHandle CompileGeometry(
            Rml::Span<const Rml::Vertex> vertices,
            Rml::Span<const int> indices) override;
    void RenderGeometry(
            Rml::CompiledGeometryHandle geometry,
            Rml::Vector2f translation,
            Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(
            Rml::Span<const Rml::byte> source,
            Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    /**
     * Stores one frame's Filament resources for RmlUi draw geometry.
     */
    struct DrawResource {
        utils::Entity entity;
        filament::VertexBuffer* vertexBuffer = nullptr;
        filament::IndexBuffer* indexBuffer = nullptr;
        filament::MaterialInstance* materialInstance = nullptr;
    };

    void releaseFrameResources();
    void createWhiteTexture();
    void releaseOwnedTextures();
    bool isRegisteredTextureSourceHandle(RenderTextureHandle textureHandle) const;

    /**
     * Resolves a RmlUi texture source URL to a renderer-owned texture handle.
     */
    struct TextureSourceBinding {
        RenderTextureHandle textureHandle = InvalidRenderTextureHandle;
        std::uint32_t width = 1u;
        std::uint32_t height = 1u;
    };

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
    filament::Texture* whiteTexture_ = nullptr;
    std::unordered_map<std::string, TextureSourceBinding> textureSources_;
    std::uint32_t framebufferWidth_ = 1u;
    std::uint32_t framebufferHeight_ = 1u;
    bool scissorEnabled_ = false;
    Rml::Rectanglei scissorRegion_;
    std::uint16_t drawOrder_ = 0u;
    std::unordered_set<RenderTextureHandle> ownedTextureHandles_;
    std::vector<DrawResource> frameResources_;
};

} // namespace quader

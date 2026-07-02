////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/texture_registry.hpp"

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
#include <cstddef>
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
    void release_frame_resources();
    void prepare(ImDrawData *draw_data, std::uint32_t framebuffer_width,
                 std::uint32_t framebuffer_height);
    void render(filament::Renderer& renderer);

private:
    /**
     * Represents a Draw Resource value used by the render extraction and Filament resource layer.
     */
    struct DrawResource {
        utils::Entity entity;
        filament::VertexBuffer *vertex_buffer = nullptr;
        filament::IndexBuffer *index_buffer = nullptr;
        std::vector<filament::MaterialInstance *> material_instances;
        std::uint32_t vertex_capacity = 0;
        std::uint32_t index_capacity = 0;
        std::size_t primitive_capacity = 0;
    };

    void create_font_texture();
    DrawResource &ensure_draw_resource(std::size_t slot,
                                       std::uint32_t vertex_count,
                                       std::uint32_t index_count,
                                       std::size_t primitive_count,
                                       std::uint32_t framebuffer_width,
                                       std::uint32_t framebuffer_height);
    void destroy_draw_resource(DrawResource &resource);
    void destroy_all_frame_resources();
    void hide_unused_frame_resources(std::size_t first_unused_slot);

    filament::Engine* engine_ = nullptr;
    filament::Material* material_ = nullptr;
    TextureRegistry* textures_ = nullptr;
    filament::Scene *clear_scene_ = nullptr;
    filament::View *clear_view_ = nullptr;
    filament::Skybox *clear_skybox_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::View* view_ = nullptr;
    filament::Camera* camera_ = nullptr;
    utils::Entity camera_entity_;
    filament::Texture *font_texture_ = nullptr;
    ImTextureID font_texture_id_ = ImTextureID_Invalid;
    std::vector<DrawResource> frame_resources_;
};

} // namespace quader

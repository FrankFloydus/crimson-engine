////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Scene.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

#include <cstdint>
#include <optional>

namespace quader {

/**
 * Owns Viewport Grid Renderer behavior within the render extraction and Filament resource layer.
 */
class ViewportGridRenderer {
public:
    ViewportGridRenderer() = default;
    ViewportGridRenderer(const ViewportGridRenderer&) = delete;
    ViewportGridRenderer& operator=(const ViewportGridRenderer&) = delete;
    ViewportGridRenderer(ViewportGridRenderer&&) = delete;
    ViewportGridRenderer& operator=(ViewportGridRenderer&&) = delete;
    ~ViewportGridRenderer();

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    filament::Material &ground_grid_material,
                    std::uint8_t grid_layer_mask);
    void shutdown();

    void update(const ViewportCameraController &controller,
                std::uint32_t viewport_width, std::uint32_t viewport_height,
                float grid_world_spacing, const ViewportSettings &settings,
                bool visible, bool instance_edit_context = false);

    bool visible() const {
        return visible_;
    }

private:
  void create_ground_grid();
  void set_ground_grid_visible(bool visible);
  void update_ground_grid_mesh(float width, float height);
  void destroy_ground_grid();

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Material *ground_grid_material_ = nullptr;
  utils::Entity ground_grid_entity_;
  filament::VertexBuffer *ground_grid_vertex_buffer_ = nullptr;
  filament::IndexBuffer *ground_grid_index_buffer_ = nullptr;
  filament::MaterialInstance *ground_grid_material_instance_ = nullptr;
  float ground_grid_mesh_width_ = 0.0f;
  float ground_grid_mesh_height_ = 0.0f;
  std::optional<std::uint64_t> presentation_signature_;
  bool visible_ = true;
  std::uint8_t grid_layer_mask_ = 0x04u;
};

} // namespace quader

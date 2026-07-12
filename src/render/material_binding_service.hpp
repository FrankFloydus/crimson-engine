////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "assets/material_document.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/components.hpp"

#include <filament/Material.h>
#include <filament/MaterialInstance.h>

#include <cstdint>

namespace quader {

class TextureBindingService;
class ViewportSettings;

/**
 * Builds the narrow signature used to avoid rewriting unchanged mesh-surface
 * grid uniforms on every presentation frame.
 */
[[nodiscard]] std::uint64_t
mesh_surface_grid_signature(float grid_world_spacing,
                            const ViewportSettings &settings);

/**
 * Coordinates Material Binding Service responsibilities for the render extraction and Filament resource layer.
 */
class MaterialBindingService {
public:
    MaterialBindingService() = default;
    MaterialBindingService(const MaterialBindingService&) = delete;
    MaterialBindingService& operator=(const MaterialBindingService&) = delete;
    MaterialBindingService(MaterialBindingService&&) = delete;
    MaterialBindingService& operator=(MaterialBindingService&&) = delete;

    void initialize(filament::Material &shaded_mesh_material,
                    filament::Material &rendered_mesh_material,
                    TextureBindingService &texture_binding);
    void shutdown();

    bool initialized() const {
      return shaded_mesh_material_ != nullptr &&
             rendered_mesh_material_ != nullptr && texture_binding_ != nullptr;
    }

    std::uint64_t material_revision(const AssetRegistry &assets,
                                    const MaterialComponent *material) const;
    std::uint64_t
    material_revision(const RenderMaterialBindingSnapshot &material) const;
    filament::MaterialInstance *create_mesh_material_instance(
        const AssetRegistry &assets, const MaterialDocument *material,
        bool double_sided, bool editor_unlit = false) const;
    filament::MaterialInstance *create_mesh_material_instance(
        const RenderMaterialAssetSnapshot *material, bool double_sided,
        bool editor_unlit = false) const;
    void apply_mesh_surface_grid(filament::MaterialInstance &material_instance,
                                 float grid_world_spacing,
                                 const ViewportSettings &settings) const;

  private:
    filament::Material *shaded_mesh_material_ = nullptr;
    filament::Material *rendered_mesh_material_ = nullptr;
    TextureBindingService *texture_binding_ = nullptr;
};

} // namespace quader

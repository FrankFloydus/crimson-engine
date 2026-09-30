////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "assets/material_document.hpp"
#include "render/material_evaluation_snapshot.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/components.hpp"

#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <math/mat3.h>
#include <math/mat4.h>

#include <cstdint>
#include <span>

namespace quader {

class TextureBindingService;
class ViewportSettings;
struct InstanceEditStyle;
enum class MaterialOpacity;

// Matches the shipping Filament instance limit; mappings never split geometry.
inline constexpr std::size_t kMeshTextureMappingCapacity = 64u;

/** Binds occurrence-local UV placement in the same order as instance transforms.
 * An empty span initializes the ordinary-renderable identity slot. */
void bind_mesh_texture_mappings(
    filament::MaterialInstance &material_instance,
    std::span<const QMat4> sampling_from_local = {});

/// Publishes prevalidated, equally sized occurrence palettes for a rigid coarse
/// mesh chunk. Each span contains 1..kMeshTextureMappingCapacity entries; CPU
/// preparation is complete before this allocation-free publication step.
void bind_coarse_mesh_parameters(
    filament::MaterialInstance &material_instance,
    std::span<const filament::math::mat4f> sampling_from_local,
    std::span<const filament::math::mat3f> sampling_cofactors,
    std::span<const filament::math::mat3f> world_normal_from_local);

/** Binds the shared Standard Material UV transform to a Filament instance. */
void bind_standard_material_texture_transform(
    filament::MaterialInstance &material_instance,
    const RenderStandardMaterialTextureTransformSnapshot &transform);

/** Binds the default shadow-free modeling light used by Shaded mode. */
void bind_default_shaded_lighting(
    filament::MaterialInstance &material_instance);

/** Binds the shared instance-edit context color and strength controls for mesh
 * and scene-wire material families. The per-renderable dim flag stays separate. */
void bind_instance_edit_context_style(
    filament::MaterialInstance &material_instance,
    const InstanceEditStyle &style);

/**
 * Builds the narrow signature used to avoid rewriting unchanged viewport mesh
 * uniforms on every presentation frame.
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

    void bind_scene(RenderSceneBindingKey scene_binding);
    void set_project_material_resolver(
        const RenderProjectMaterialResolver *resolver) {
      project_material_resolver_ = resolver;
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
    /// Resolves the same default override as material creation without loading
    /// images; only a decoded opaque base color or solid fallback returns true.
    [[nodiscard]] bool material_is_known_opaque(
        const RenderMaterialAssetSnapshot *material) const;
    /// Distinguishes an unloaded image from known alpha without loading pixels.
    [[nodiscard]] MaterialOpacity material_opacity(
        const RenderMaterialAssetSnapshot *material) const;
    filament::MaterialInstance *duplicate_mesh_material_instance(
        const filament::MaterialInstance *source) const;
    void destroy_mesh_material_instance(
        filament::Engine &engine,
        filament::MaterialInstance *&material_instance) const;
    void apply_mesh_surface_grid(filament::MaterialInstance &material_instance,
                                 float grid_world_spacing,
                                 const ViewportSettings &settings) const;
    void apply_shaded_lighting(
        filament::MaterialInstance &material_instance,
        const ViewportSettings &settings) const;
    void apply_instance_edit_dim(
        filament::MaterialInstance &material_instance, bool dimmed) const;

  private:
    [[nodiscard]] const RenderMaterialAssetSnapshot *effective_material(
        const RenderMaterialAssetSnapshot *material) const;
    filament::Material *shaded_mesh_material_ = nullptr;
    filament::Material *rendered_mesh_material_ = nullptr;
    TextureBindingService *texture_binding_ = nullptr;
    const RenderProjectMaterialResolver *project_material_resolver_ = nullptr;
};

} // namespace quader

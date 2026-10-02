////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/instance_edit_style.hpp"
#include "render/component_selection_renderer.hpp"
#include "render/overlay/overlay_stencil_reset.hpp"
#include "render/overlay/selected_mesh_outline_renderer.hpp"
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
#include <optional>

namespace quader {

class SelectedMeshOutlineMaskCache;
struct RenderOverlaySelectedObjectSnapshot;
struct RenderOverlaySelectionSnapshot;

/**
 * Selects a viewport's target and whether its swapchain performs sRGB encoding.
 */
enum class ViewportRenderSurface {
    Texture,
    Swapchain,
    SrgbSwapchain,
};

/**
 * Selects a renderer-owned material diagnostic for one Filament view.
 */
enum class ViewportMaterialDebugMode : std::uint8_t {
    None = 0,
    DiffuseOnly = 1,
    SpecularOnly = 2,
    Albedo = 3,
    Roughness = 4,
    Metalness = 5,
    Normal = 6,
    Uv = 7,
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

    void
    initialize(filament::Engine &engine, filament::Scene &scene,
               TextureRegistry &texture_registry,
               filament::Material &ground_grid_material,
               filament::Material &overlay_stencil_reset_material,
               std::uint8_t grid_layer_mask,
               ViewportRenderSurface surface = ViewportRenderSurface::Texture);
    void shutdown();

    /** Enables retained GPU selection coverage when the backend supports it. */
    bool configure_selected_mesh_outlines(SelectedMeshOutlineMaskCache &cache,
                                         filament::Material &material,
                                         std::uint8_t pane_layer_mask);
    bool update_selected_mesh_outlines(
        const RenderOverlaySelectedObjectSnapshot *snapshot,
        const RenderOverlaySelectionSnapshot *hierarchy,
        const ViewportPersistentOverlayStyleSnapshot &style, bool visible);

    void resize(std::uint32_t width, std::uint32_t height);
    /** Native raster bounds; width()/height() remain the visible camera extent. */
    void set_presentation_bounds(std::uint32_t backing_width,
                                 std::uint32_t backing_height,
                                 std::uint32_t crop_left,
                                 std::uint32_t crop_top);
    void update_camera(const ViewportCameraController &controller);
    void apply_scene_settings(const SceneSettings &settings,
                              bool rendered_mode,
                              const ViewportSettings &viewport_settings);
    void update_ground_grid(const ViewportCameraController &controller,
                            float grid_world_spacing,
                            const ViewportSettings &settings,
                            bool visible = true);
    void set_instance_edit_context_active(bool active,
                                          const ViewportSettings &settings);
    void set_overlay_visible(bool visible);
    void set_scene_visible(bool visible);
    void set_scene_layer_mask(std::uint8_t layer_mask);
    void set_shadowing_enabled(bool enabled);
    /** Updates cached per-view diagnostics, Solid mode, and mesh-grid visibility. */
    void set_material_presentation(ViewportMaterialDebugMode mode,
                                   bool solid, bool mesh_grid_visible = true);
    void set_material_debug_mode(ViewportMaterialDebugMode mode);
    void render(filament::Renderer& renderer);
    /// Supplies the derived selection capability without exposing Filament handles.
    ComponentSelectionRenderer &component_selection() { return component_selection_renderer_; }
    bool configure_component_selection(filament::Material &material, filament::Texture &fallback);

    RenderTextureHandle texture_handle() const {
      return render_target_.texture_handle();
    }
    RenderTextureHandle overlay_texture_handle() const {
      return overlay_render_target_.texture_handle();
    }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    float texture_max_u() const;
    float texture_max_v() const;
    /** Counts effective scene-setting applications since initialization. */
    std::uint64_t scene_settings_apply_count() const {
      return scene_settings_apply_count_;
    }

  private:
    void update_visible_layers();
    void apply_tone_mapping(SceneToneMapping tone_mapping, bool enabled);
    void destroy_color_grading();
    void reset_presentation_cache();
    void update_presentation_bounds();
    void update_bloom_presentation(bool force);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    TextureRegistry *texture_registry_ = nullptr;
    filament::View* view_ = nullptr;
    filament::View *overlay_view_ = nullptr;
    filament::Camera* camera_ = nullptr;
    filament::Camera *overlay_camera_ = nullptr;
    filament::ColorGrading *color_grading_ = nullptr;
    utils::Entity camera_entity_;
    utils::Entity overlay_camera_entity_;
    ViewportGridRenderer grid_renderer_;
    OverlayStencilReset overlay_stencil_reset_;
    SelectedMeshOutlineRenderer selected_mesh_outline_renderer_;
    SelectedMeshOutlineMaskCache *selected_mesh_outline_cache_ = nullptr;
    ViewportRenderPassCoordinator render_pass_coordinator_;
    ViewportRenderTarget render_target_;
    ComponentSelectionRenderer component_selection_renderer_;
    ViewportRenderTarget overlay_render_target_;
    TextureSampleColorSpace scene_texture_sample_color_space_ =
        TextureSampleColorSpace::LinearSrgb;
    ViewportRenderSurface surface_ = ViewportRenderSurface::Texture;
    std::uint32_t width_ = 1u;
    std::uint32_t height_ = 1u;
    std::uint32_t backing_width_ = 1u;
    std::uint32_t backing_height_ = 1u;
    std::uint32_t crop_left_ = 0u;
    std::uint32_t crop_top_ = 0u;
    bool has_presentation_bounds_ = false;
    SceneToneMapping color_grading_tone_mapping_ = SceneToneMapping::Aces;
    bool has_color_grading_tone_mapping_ = false;
    bool instance_edit_context_active_ = false;
    ViewportMaterialDebugMode material_debug_mode_ =
        ViewportMaterialDebugMode::None;
    bool solid_material_presentation_ = false;
    bool mesh_grid_visible_ = true;
    bool has_material_presentation_ = false;
    InstanceEditStyle instance_edit_style_;
    std::optional<std::uint64_t> camera_presentation_signature_;
    std::optional<std::uint64_t> presentation_bounds_signature_;
    std::optional<filament::View::BloomOptions> semantic_bloom_options_;
    std::optional<std::uint32_t> bloom_presentation_resolution_;
    std::optional<std::uint64_t> scene_settings_presentation_signature_;
    std::uint64_t scene_settings_apply_count_ = 0u;
    std::optional<bool> shadowing_enabled_;
};

} // namespace quader

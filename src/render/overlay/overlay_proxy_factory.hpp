////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/overlay_renderable_proxy.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace filament {
class Engine;
class Material;
class MaterialInstance;
class Scene;
class Texture;
} // namespace filament

namespace quader {

/**
 * Creates Overlay Proxy Factory resources for the render extraction and Filament resource layer.
 */
class OverlayProxyFactory {
public:
    OverlayProxyFactory() = default;
    OverlayProxyFactory(const OverlayProxyFactory&) = delete;
    OverlayProxyFactory& operator=(const OverlayProxyFactory&) = delete;
    OverlayProxyFactory(OverlayProxyFactory&&) = delete;
    OverlayProxyFactory& operator=(OverlayProxyFactory&&) = delete;

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    filament::Material &overlay_material,
                    filament::Material &overlay_line_material,
                    filament::Material &edit_wire_line_material,
                    filament::Material &overlay_point_material,
                    filament::Material &overlay_icon_material,
                    const std::filesystem::path &resource_root);
    void shutdown();

    bool initialized() const {
      return engine_ != nullptr && scene_ != nullptr &&
             overlay_material_ != nullptr &&
             overlay_line_material_ != nullptr &&
             edit_wire_line_material_ != nullptr &&
             overlay_point_material_ != nullptr &&
             overlay_icon_material_ != nullptr;
    }

    std::vector<OverlayRenderableProxy>
    create_layer_proxies(const OverlayLayerBuildPlan &plan,
                         std::uint8_t layer_mask);
    bool update_layer_proxies(std::vector<OverlayRenderableProxy> &proxies,
                              const OverlayLayerBuildPlan &plan,
                              std::uint8_t layer_mask);
    /// Uploads only retained world-position attributes. Static quad topology,
    /// controls, colors, indices and material/depth state remain untouched.
    bool update_position_streams(OverlayRenderableProxy &proxy);
    /// Changes visibility without rebuilding or re-uploading retained overlay
    /// geometry. Used to keep very large component overlays off the drag hot
    /// path while the dynamic mesh preview is visible.
    void set_layer_mask(const std::vector<OverlayRenderableProxy> &proxies,
                        std::uint8_t layer_mask);
    void destroy(OverlayRenderableProxy& proxy);

    static std::size_t planned_proxy_count(const OverlayLayerBuildPlan &plan);

  private:
    void create_triangle_proxy(const OverlayFaceFillBatch &batch,
                               std::uint8_t layer_mask,
                               std::vector<OverlayRenderableProxy> &proxies);
    void create_line_proxy(const OverlayLineBatch &batch,
                           std::uint8_t layer_mask,
                           std::vector<OverlayRenderableProxy> &proxies);
    void create_edit_wire_proxy(const OverlayEditWireBatch &batch,
                                std::uint8_t layer_mask,
                                std::vector<OverlayRenderableProxy> &proxies);
    void create_point_proxy(const OverlayPointBatch &batch,
                            std::uint8_t layer_mask,
                            std::vector<OverlayRenderableProxy> &proxies);
    void create_icon_proxy(const OverlayIconBatch &batch,
                           std::uint8_t layer_mask,
                           std::vector<OverlayRenderableProxy> &proxies);
    void update_triangle_proxy(OverlayRenderableProxy &proxy,
                               const OverlayFaceFillBatch &batch,
                               std::uint8_t layer_mask);
    void update_line_proxy(OverlayRenderableProxy &proxy,
                           const OverlayLineBatch &batch,
                           std::uint8_t layer_mask);
    void update_edit_wire_proxy(OverlayRenderableProxy &proxy,
                                const OverlayEditWireBatch &batch,
                                std::uint8_t layer_mask);
    void update_point_proxy(OverlayRenderableProxy &proxy,
                            const OverlayPointBatch &batch,
                            std::uint8_t layer_mask);
    void update_icon_proxy(OverlayRenderableProxy &proxy,
                           const OverlayIconBatch &batch,
                           std::uint8_t layer_mask);
    void configure_overlay_material(filament::MaterialInstance &material,
                                    const OverlayRenderState &state) const;
    void load_icon_textures(const std::filesystem::path &resource_root);
    void destroy_icon_textures();
    [[nodiscard]] std::size_t
    planned_loaded_proxy_count(const OverlayLayerBuildPlan &plan) const;
    [[nodiscard]] filament::Texture *
    texture_for_icon(ViewportOverlayIconKind icon) const;

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material *overlay_material_ = nullptr;
    filament::Material *overlay_line_material_ = nullptr;
    filament::Material *edit_wire_line_material_ = nullptr;
    filament::Material *overlay_point_material_ = nullptr;
    filament::Material *overlay_icon_material_ = nullptr;
    filament::Texture *point_light_icon_texture_ = nullptr;
};

} // namespace quader

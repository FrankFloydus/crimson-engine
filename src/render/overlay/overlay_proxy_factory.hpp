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
#include <memory>
#include <span>
#include <vector>

namespace filament {
class Engine;
class Material;
class MaterialInstance;
class Scene;
class Texture;
} // namespace filament

namespace quader {

/// Counts actual buffer submissions, including generic rebuild paths.
struct OverlayProxyUploadStats {
    std::uint64_t geometry_creation_count = 0u;
    std::uint64_t position_upload_count = 0u;
    std::uint64_t index_upload_count = 0u;
    std::uint64_t static_upload_count = 0u;
    std::uint64_t appearance_upload_count = 0u;
    std::uint64_t visibility_upload_count = 0u;
    /// Fragmented edits consolidated before submission to bound command count.
    std::uint64_t retained_edit_wire_consolidated_upload_count = 0u;
    std::uint64_t retained_edit_wire_uploaded_slot_count = 0u;
    std::uint64_t shared_position_upload_count = 0u;
    std::uint64_t shared_position_bind_count = 0u;
    std::uint64_t shared_position_binding_upload_count = 0u;
};

/// Several stencil/color draws select ranges from one retained face arena.
struct OverlayRetainedFaceDraw {
    OverlayRenderState state;
    std::uint32_t first_index = 0u;
    std::uint32_t index_count = 0u;
};

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
    /// Ordered component markers share geometry across selection classes/passes.
    std::vector<OverlayRenderableProxy> create_component_point_proxies(
        std::span<const ViewportOverlayPoint *const> points,
        std::span<const std::size_t> frame_indices,
        const ViewportOverlayCamera &camera, std::uint16_t blend_order_bias,
        std::uint8_t layer_mask);
    /// Caller has verified identical ordered element identities and positions.
    bool update_component_point_appearance(
        OverlayRenderableProxy &proxy,
        std::span<const ViewportOverlayPoint *const> points,
        std::span<const std::size_t> frame_indices,
        const ViewportOverlayCamera &camera, std::uint16_t blend_order_bias,
        std::uint8_t layer_mask);
    /// Owns validated slot ranges, active indices and asynchronous upload bytes.
    struct RetainedEditWireUpdate;
    /// A null destination prepares all slots for a new capacity. Retained
    /// updates require sorted unique dirty slots and preserve active index order.
    [[nodiscard]] static std::shared_ptr<RetainedEditWireUpdate>
    prepare_retained_edit_wire_update(
        const OverlayRenderableProxy *existing,
        std::span<const ViewportOverlayLine> slots,
        std::span<const std::uint32_t> dirty_slots,
        std::span<const std::uint32_t> active_indices,
        std::span<const OverlayPositionStreamBinding> active_bindings,
        bool indices_changed);
    /// Uses presentation state only; its mesh is not expanded or consumed.
    OverlayRenderableProxy create_retained_edit_wire_proxy(
        const OverlayEditWireBatch &presentation, std::uint8_t layer_mask,
        std::shared_ptr<RetainedEditWireUpdate> prepared);
    /// Consumes the complete candidate once without further host allocations.
    bool update_retained_edit_wire_proxy(
        OverlayRenderableProxy &proxy,
        const OverlayEditWireBatch &presentation, std::uint8_t layer_mask,
        std::shared_ptr<RetainedEditWireUpdate> prepared);
    [[nodiscard]] OverlayProxyUploadStats stats() const { return upload_stats_; }
    OverlayRenderableProxy create_retained_face_proxy(
        std::span<const ViewportOverlayTriangle> source_triangles,
        std::span<const std::uint32_t> triangle_colors,
        std::span<const std::uint32_t> indices,
        std::span<const OverlayRetainedFaceDraw> draws,
        std::uint8_t layer_mask);
    bool update_retained_face_proxy(
        OverlayRenderableProxy &proxy,
        std::span<const std::uint32_t> triangle_colors,
        std::span<const std::uint32_t> indices,
        std::span<const OverlayRetainedFaceDraw> draws,
        std::uint8_t layer_mask, bool indices_changed);
    /// One world-position upload serves every fully mapped preview proxy.
    /// Layout indices remain stable until the caller clears or rebinds them.
    bool upload_shared_positions(std::span<const OverlayFloat3> positions);
    /// Indices cover each allocated vertex, including inactive retained slots.
    /// Unsupported materials and indices outside the uploaded stream reject
    /// without changing the proxy. CPU position arrays remain untouched.
    /// Omitted support indices follow the primary endpoint, keeping unsupported
    /// incident surfaces degenerate during the shared-position preview path.
    bool bind_shared_positions(
        OverlayRenderableProxy &proxy,
        std::span<const std::uint32_t> primary_indices,
        std::span<const std::uint32_t> secondary_indices = {},
        std::span<const std::uint32_t> support0_indices = {},
        std::span<const std::uint32_t> support1_indices = {});
    /// Restores the proxy's retained CPU streams and ordinary material path.
    bool clear_shared_positions(OverlayRenderableProxy &proxy);
    /// Uploads only retained world-position attributes. Static quad topology,
    /// controls, colors, indices and material/depth state remain untouched.
    bool update_position_streams(OverlayRenderableProxy &proxy);
    /// Uploads only changed point visibility flags. Each mapped marker keeps
    /// its position, static quad topology, indices and material/depth state.
    bool update_point_visibility(
        OverlayRenderableProxy &proxy,
        std::span<const std::uint8_t> frame_visibility);
    /// Refreshes camera-dependent clip/bias uniforms without rebuilding or
    /// uploading retained line and point geometry.
    void update_camera_state(
        const std::vector<OverlayRenderableProxy> &proxies,
        const ViewportOverlayCamera &camera);
    /// Changes visibility without rebuilding or re-uploading retained overlay
    /// geometry. Used to keep very large component overlays off the drag hot
    /// path while the dynamic mesh preview is visible.
    void set_layer_mask(const std::vector<OverlayRenderableProxy> &proxies,
                        std::uint8_t layer_mask);
    void destroy(OverlayRenderableProxy& proxy);

    static std::size_t planned_proxy_count(const OverlayLayerBuildPlan &plan);

  private:
    void configure_edit_wire_material(filament::MaterialInstance &material,
                                      const OverlayEditWireBatch &batch) const;
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
    filament::Texture *shared_position_texture_ = nullptr;
    filament::Texture *shared_position_fallback_texture_ = nullptr;
    std::vector<filament::MaterialInstance *> shared_position_materials_;
    std::size_t shared_position_count_ = 0u;
    OverlayProxyUploadStats upload_stats_;
};

} // namespace quader

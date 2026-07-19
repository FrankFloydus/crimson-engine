////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/instance_batch_planner.hpp"
#include "render/render_layers.hpp"
#include "render/scene_render_extraction.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <cstdint>
#include <memory>
#include <span>

namespace quader {

class MaterialBindingService;
class ViewportSettings;

/**
 * Creates Mesh Renderable Factory resources for the render extraction and Filament resource layer.
 */
class MeshRenderableFactory {
public:
    MeshRenderableFactory();
    MeshRenderableFactory(const MeshRenderableFactory&) = delete;
    MeshRenderableFactory& operator=(const MeshRenderableFactory&) = delete;
    MeshRenderableFactory(MeshRenderableFactory&&) = delete;
    MeshRenderableFactory& operator=(MeshRenderableFactory&&) = delete;
    ~MeshRenderableFactory();

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    MaterialBindingService &material_binding);
    void shutdown();

    bool initialized() const {
      return engine_ != nullptr && scene_ != nullptr &&
             material_binding_ != nullptr;
    }

    MeshRenderableProxy
    create(EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
           const RenderMaterialBindingSnapshot *material,
           const QMat4 &world_transform, bool cast_shadows, bool double_sided,
           std::uint8_t layer_mask = kViewportRenderedSceneLayerMask,
           bool editor_unlit = false, bool depth_only = false,
           std::uint8_t channel = kViewportDefaultRenderableChannel,
           bool dynamic_geometry = false,
           std::span<const QVec3> dynamic_positions = {});
    MeshRenderableProxy create_shared_instance(
        EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
        const MeshRenderableProxy &prototype, const QMat4 &world_transform,
        std::uint8_t layer_mask = kViewportRenderedSceneLayerMask,
        std::uint8_t channel = kViewportDefaultRenderableChannel);
    MeshRenderableProxy create_shared_geometry(
        EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
        const RenderMaterialBindingSnapshot *material,
        const MeshRenderableProxy &geometry_source,
        const QMat4 &world_transform, bool double_sided,
        std::uint8_t layer_mask, bool editor_unlit = false,
        bool depth_only = false,
        std::uint8_t channel = kViewportDefaultRenderableChannel);
    MeshRenderableProxy create_explicit_instances(
        const RenderMeshAssetSnapshot &mesh,
        const RenderMaterialBindingSnapshot *material,
        MeshPrimitiveSpan primitive, std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds, bool cast_shadows,
        bool receive_shadows, bool double_sided, std::uint8_t layer_mask,
        bool editor_unlit = false, bool depth_only = false,
        std::uint8_t channel = kViewportDefaultRenderableChannel,
        std::uint8_t presentation_flags = 0u);
    MeshRenderableProxy create_instance_selection_fill(
        const RenderMeshAssetSnapshot &mesh, filament::Material &material,
        std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds, std::uint32_t color,
        std::uint16_t blend_order);
    bool update_explicit_instances(
        const MeshRenderableProxy &proxy,
        std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds);
    void update_transform(const MeshRenderableProxy &proxy,
                          const QMat4 &world_transform);
    void update_layer_mask(const MeshRenderableProxy &proxy,
                           std::uint8_t layer_mask);
    void update_bounds(const MeshRenderableProxy &proxy,
                       const RenderMeshAssetSnapshot &mesh);
    void update_bounds(const MeshRenderableProxy &proxy,
                       std::span<const QVec3> positions);
    void update_surface_grid(const MeshRenderableProxy &proxy,
                             float grid_world_spacing,
                             const ViewportSettings &settings);
    void update_shaded_lighting(const MeshRenderableProxy &proxy,
                                const ViewportSettings &settings);
    bool update_dynamic_geometry(const MeshRenderableProxy &proxy,
                                 const RenderMeshAssetSnapshot &mesh,
                                 std::span<const QVec3> dynamic_positions = {});
    void remove_from_scene(const MeshRenderableProxy &proxy);
    void destroy(MeshRenderableProxy& proxy);
    void destroy_detached(MeshRenderableProxy &proxy);

    static std::uint64_t mesh_asset_signature(std::uint64_t hash,
                                              const MeshAsset &mesh);

    /// Reports shared GPU geometry resources, reuse, and upload bytes.
    struct GeometryCacheStats final {
      std::size_t resource_count = 0u;
      std::uint64_t cache_hit_count = 0u;
      std::uint64_t upload_count = 0u;
      std::uint64_t uploaded_vertex_bytes = 0u;
      std::uint64_t uploaded_index_bytes = 0u;
    };

    [[nodiscard]] GeometryCacheStats geometry_cache_stats() const;

  private:
    struct GeometryCacheState;
    bool acquire_geometry(const RenderMeshAssetSnapshot &mesh,
                          MeshRenderableProxy &proxy);
    bool acquire_dynamic_geometry(const RenderMeshAssetSnapshot &mesh,
                                  MeshRenderableProxy &proxy,
                                  std::span<const QVec3> dynamic_positions);
    void release_geometry(MeshRenderableProxy &proxy);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MaterialBindingService *material_binding_ = nullptr;
    std::unique_ptr<GeometryCacheState> geometry_cache_;
};

} // namespace quader

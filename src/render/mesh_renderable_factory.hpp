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

#include <cstddef>
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

    // A bounds override is borrowed only for the duration of the call and is
    // copied into Filament. Sibling renderables can therefore reuse one
    // caller-computed local AABB without sharing lifetime-sensitive storage.
    MeshRenderableProxy
    create(EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
           const RenderMaterialBindingSnapshot *material,
           const QMat4 &world_transform, bool cast_shadows, bool double_sided,
           std::uint8_t layer_mask = kViewportRenderedSceneLayerMask,
           bool editor_unlit = false, bool depth_only = false,
           std::uint8_t channel = kViewportDefaultRenderableChannel,
           bool dynamic_geometry = false,
           std::span<const QVec3> dynamic_positions = {},
           const RenderInstanceBounds *local_bounds_override = nullptr);
    MeshRenderableProxy create_shared_instance(
        EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
        const MeshRenderableProxy &prototype, const QMat4 &world_transform,
        std::uint8_t layer_mask = kViewportRenderedSceneLayerMask,
        std::uint8_t channel = kViewportDefaultRenderableChannel,
        bool receive_shadows = false,
        const RenderInstanceBounds *local_bounds_override = nullptr);
    MeshRenderableProxy create_shared_geometry(
        EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
        const RenderMaterialBindingSnapshot *material,
        const MeshRenderableProxy &geometry_source,
        const QMat4 &world_transform, bool double_sided,
        std::uint8_t layer_mask, bool editor_unlit = false,
        bool depth_only = false,
        std::uint8_t channel = kViewportDefaultRenderableChannel,
        const RenderInstanceBounds *local_bounds_override = nullptr);
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
    /// Uploads a compact visible prefix and changes the active instance count
    /// without replacing the entity, geometry, material, or InstanceBuffer.
    /// A false result after a count-changing build leaves the renderable
    /// component detached so the cache can immediately destroy/full-rebuild
    /// the still-owned proxy deterministically.
    bool rebind_explicit_instances(
        const MeshRenderableProxy &proxy, MeshPrimitiveSpan primitive,
        std::span<const QMat4> active_world_transforms,
        const RenderInstanceBounds &world_bounds, bool cast_shadows,
        bool receive_shadows, bool double_sided, std::uint8_t layer_mask,
        bool depth_only = false,
        std::uint8_t channel = kViewportDefaultRenderableChannel);
    void update_transform(const MeshRenderableProxy &proxy,
                          const QMat4 &world_transform);
    void update_layer_mask(const MeshRenderableProxy &proxy,
                           std::uint8_t layer_mask);
    void update_bounds(const MeshRenderableProxy &proxy,
                       const RenderMeshAssetSnapshot &mesh);
    void update_bounds(const MeshRenderableProxy &proxy,
                       std::span<const QVec3> positions);
    void update_bounds(const MeshRenderableProxy &proxy,
                       const RenderInstanceBounds &local_bounds);
    void update_surface_grid(const MeshRenderableProxy &proxy,
                             float grid_world_spacing,
                             const ViewportSettings &settings);
    void update_shaded_lighting(const MeshRenderableProxy &proxy,
                                const ViewportSettings &settings);
    /// Refreshes every mutable stream of a topology-retained preview. The
    /// vertex/index counts stay fixed, while deformation-derived normals,
    /// tangent frames, UVs, surface-grid projections, and triangulation may
    /// change together with positions.
    bool update_dynamic_geometry(const MeshRenderableProxy &proxy,
                                 const RenderMeshAssetSnapshot &mesh,
                                 std::span<const QVec3> dynamic_positions = {},
                                 const RenderInstanceBounds *
                                     local_bounds_override = nullptr);
    /// Rebinds one or more existing static renderables to a replacement
    /// immutable geometry payload without recreating their entities or
    /// material instances. Every proxy is validated and every replacement
    /// geometry reference is acquired before any renderable is mutated.
    bool try_rebind_static_geometry(
        std::span<MeshRenderableProxy *> proxies,
        const RenderMeshAssetSnapshot &mesh);
    void remove_from_scene(const MeshRenderableProxy &proxy);
    /// Detaches an already-collected proxy generation with one Filament scene
    /// call so retirement cost does not scale in renderer API calls.
    void remove_from_scene(std::span<const utils::Entity> entities);
    void destroy(MeshRenderableProxy& proxy);
    void destroy_detached(MeshRenderableProxy &proxy);

    static std::uint64_t mesh_asset_signature(std::uint64_t hash,
                                              const MeshAsset &mesh);
    /// Computes one renderer-neutral local AABB for reuse across siblings.
    [[nodiscard]] static RenderInstanceBounds
    compute_local_bounds(const RenderMeshAssetSnapshot &mesh);
    [[nodiscard]] static RenderInstanceBounds
    compute_local_bounds(std::span<const QVec3> positions);

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

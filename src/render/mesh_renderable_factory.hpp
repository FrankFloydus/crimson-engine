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
struct MeshCollisionGeometryPayload;
struct ViewportSettingColor;

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
    /// Owns dynamic vertex/index buffers without an entity or material. Destroy
    /// every borrowing instance chunk before destroying this owner; upload each
    /// validated family update through the owner exactly once.
    MeshRenderableProxy create_dynamic_geometry(
        const RenderMeshAssetSnapshot &mesh,
        std::span<const QVec3> dynamic_positions = {});
    static constexpr std::size_t kEditableSurfaceMaximumOccurrences = 64u;
    static constexpr std::size_t kEditableSurfaceMaximumVertices = 65536u;
    static constexpr std::size_t kEditableSurfaceMaximumIndices = 262144u;
    static constexpr float kEditableSurfaceMaximumPlacementOffset = 4096.0f;
    /// One canonical mesh occurrence. Selected primitive spans share the
    /// chunk's resolved material; an empty span includes the complete index list.
    /// Borrowed spans remain immutable until CPU preparation has joined.
    struct EditableSurfaceSource final {
      RenderMeshAssetSnapshot mesh;
      std::span<const QVec3> positions;
      std::shared_ptr<const std::vector<QVec3>> position_payload;
      std::span<const MeshPrimitiveSpan> primitives;
      QMat4 world_from_local;
      QMat4 sampling_from_local;
      std::uint64_t position_revision = 0u;
    };
    /// Owns validated immutable CPU streams, palette and material parameters.
    /// Geometry descriptors are single-use; presentation parameters are shared
    /// by the rendered, shaded and depth borrowers of one geometry owner.
    struct EditableSurfaceUpdate;
    /// Shares canonical CPU packs only across one joined preparation batch.
    /// Concurrent chunk workers may use it; it owns no Filament resources.
    struct EditableSurfacePreparationContext;
    [[nodiscard]] static std::shared_ptr<EditableSurfacePreparationContext>
    create_editable_surface_preparation_context();
    /// Register immutable stream identities before starting preparation workers.
    /// Unique sources bypass the shared-promise interner entirely.
    static void register_editable_surface_sources(
        EditableSurfacePreparationContext &context,
        std::span<const EditableSurfaceSource> sources);
    [[nodiscard]] static bool supports_editable_surface_transform(const QMat4 &transform);
    [[nodiscard]] static std::shared_ptr<EditableSurfaceUpdate>
    prepare_editable_surface_update(std::span<const EditableSurfaceSource> sources,
                                    const MeshRenderableProxy *existing = nullptr,
                                    std::shared_ptr<EditableSurfacePreparationContext> context = {});
    [[nodiscard]] static bool editable_surface_update_reuses_geometry(
        const EditableSurfaceUpdate &prepared);
    [[nodiscard]] static RenderInstanceBounds editable_surface_world_bounds(
        const EditableSurfaceUpdate &prepared);
    /// Owns combined buffers and the palette without an entity or material.
    /// Destroy all pass borrowers before destroying this owner.
    MeshRenderableProxy create_editable_surface_geometry(
        std::shared_ptr<EditableSurfaceUpdate> prepared);
    MeshRenderableProxy create_editable_surface_proxy(
        const MeshRenderableProxy &geometry_owner,
        const RenderMaterialAssetSnapshot *material,
        const RenderInstanceBounds &world_bounds, bool cast_shadows,
        bool receive_shadows, bool double_sided, std::uint8_t layer_mask,
        bool editor_unlit = false, bool depth_only = false,
        std::uint8_t channel = kViewportDefaultRenderableChannel,
        std::uint8_t presentation_flags = 0u);
    [[nodiscard]] bool validate_editable_surface_update(
        const MeshRenderableProxy &owner, const EditableSurfaceUpdate &prepared) const;
    [[nodiscard]] bool validate_editable_surface_presentation(
        const MeshRenderableProxy &proxy, const MeshRenderableProxy &owner,
        const EditableSurfaceUpdate &prepared) const;
    /// Apply only after validating the complete owner/pass publication batch.
    bool update_editable_surface_geometry(MeshRenderableProxy &owner,
        std::shared_ptr<EditableSurfaceUpdate> prepared);
    bool update_editable_surface_presentation(MeshRenderableProxy &proxy,
        const EditableSurfaceUpdate &prepared,
        const RenderInstanceBounds &world_bounds, bool cast_shadows);
    /// An optional dynamic geometry owner supplies already-created buffers.
    /// The caller guarantees identical geometry for every occurrence; the
    /// owner outlives the returned chunk and owns all subsequent uploads.
    MeshRenderableProxy create_explicit_instances(
        const RenderMeshAssetSnapshot &mesh,
        const RenderMaterialBindingSnapshot *material,
        MeshPrimitiveSpan primitive, std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds, bool cast_shadows,
        bool receive_shadows, bool double_sided, std::uint8_t layer_mask,
        bool editor_unlit = false, bool depth_only = false,
        std::uint8_t channel = kViewportDefaultRenderableChannel,
        std::uint8_t presentation_flags = 0u,
        std::size_t instance_capacity = 0u,
        const MeshRenderableProxy *geometry_source = nullptr);
    MeshRenderableProxy create_instance_selection_fill(
        const RenderMeshAssetSnapshot &mesh, filament::Material &material,
        std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds, std::uint32_t color,
        std::uint16_t blend_order, bool active_selected);
    /// Shares position-only triangle soup buffers across collision instances.
    MeshRenderableProxy create_collision_shape_fill(
        std::shared_ptr<const MeshCollisionGeometryPayload> geometry,
        filament::Material &material,
        std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds,
        const ViewportSettingColor &color, std::uint16_t blend_order);
    /// Updates the fill uniform without quantizing the chosen opacity.
    void update_collision_shape_fill_color(
        const MeshRenderableProxy &proxy, const ViewportSettingColor &color);
    bool update_explicit_instances(
        const MeshRenderableProxy &proxy,
        std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds);
    /// Converts instance transforms before a batch mutates retained geometry.
    /// The immutable result can update sibling passes without host allocation.
    struct ExplicitInstanceUpdate;
    [[nodiscard]] static std::shared_ptr<ExplicitInstanceUpdate>
    prepare_explicit_instance_update(std::span<const QMat4> world_transforms);
    /// Requires an existing renderable with the prepared exact active count.
    [[nodiscard]] bool validate_explicit_instances(
        const MeshRenderableProxy &proxy,
        const ExplicitInstanceUpdate &prepared) const;
    bool update_explicit_instances_prepared(
        const MeshRenderableProxy &proxy,
        const ExplicitInstanceUpdate &prepared,
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
    void update_texture_mapping(const MeshRenderableProxy &proxy,
                                const QMat4 &sampling_from_local);
    void update_texture_mappings(const MeshRenderableProxy &proxy,
                                 std::span<const QMat4> sampling_from_local);
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
    /// Opaque CPU upload storage; preparation never accesses Filament resources.
    struct DynamicGeometryUpdate;
    /// Callers keep borrowed inputs alive and immutable until the prepared
    /// result is consumed once; snapshot-owned geometry is pinned internally.
    /// An unconsumed equivalent update may share its immutable packed bytes;
    /// mismatched geometry or effective positions reject reuse before upload.
    [[nodiscard]] static std::shared_ptr<DynamicGeometryUpdate>
    prepare_dynamic_geometry_update(
        const RenderMeshAssetSnapshot &mesh,
        std::span<const QVec3> dynamic_positions = {},
        const std::shared_ptr<DynamicGeometryUpdate> &equivalent_update = {});
    /// Refreshes every mutable stream of a topology-retained preview. The
    /// vertex/index counts stay fixed, while deformation-derived normals,
    /// tangent frames, UVs, surface-grid projections, and triangulation may
    /// change together with positions. Only owning proxies accept uploads;
    /// an entity-less owner leaves each instance chunk's world bounds intact.
    bool update_dynamic_geometry(const MeshRenderableProxy &proxy,
                                 const RenderMeshAssetSnapshot &mesh,
                                 std::span<const QVec3> dynamic_positions = {},
                                 const RenderInstanceBounds *
                                     local_bounds_override = nullptr,
                                 std::shared_ptr<DynamicGeometryUpdate> prepared = {});
    /// Rebinds one or more existing static renderables to a replacement
    /// immutable geometry payload without recreating their entities or
    /// material instances. Every proxy is validated and every replacement
    /// geometry reference is acquired before any renderable is mutated.
    bool try_rebind_static_geometry(
        std::span<MeshRenderableProxy *> proxies,
        const RenderMeshAssetSnapshot &mesh);
    /// Borrows one primitive's replacement geometry and complete world bounds.
    struct InstanceGeometryRebind final {
      MeshRenderableProxy *proxy = nullptr;
      const RenderMeshAssetSnapshot *mesh = nullptr;
      MeshPrimitiveSpan primitive;
      RenderInstanceBounds world_bounds;
    };
    /// Validates the complete batch and acquires every immutable replacement
    /// before rebinding, retaining entities, materials and InstanceBuffers.
    bool try_rebind_instance_geometry(
        std::span<const InstanceGeometryRebind> requests);
    void remove_from_scene(const MeshRenderableProxy &proxy);
    /// Detaches an already-collected proxy generation with one Filament scene
    /// call so retirement cost does not scale in renderer API calls.
    void remove_from_scene(std::span<const utils::Entity> entities);
    /// Retires all borrowing renderable components before their materials are
    /// released. Proxy-owned buffers, transforms and entity IDs remain valid.
    void destroy_renderable_components(std::span<const utils::Entity> entities);
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
      std::uint64_t dynamic_geometry_create_count = 0u;
      std::uint64_t dynamic_geometry_upload_count = 0u;
      std::uint64_t dynamic_uploaded_vertex_bytes = 0u;
      std::uint64_t dynamic_uploaded_index_bytes = 0u;
      std::size_t editable_surface_live_geometry_count = 0u;
      std::uint64_t editable_surface_geometry_create_count = 0u;
      std::uint64_t editable_surface_attribute_pack_count = 0u;
      std::uint64_t editable_surface_canonical_reuse_count = 0u;
      std::uint64_t editable_surface_position_upload_count = 0u;
      std::uint64_t editable_surface_attribute_upload_count = 0u;
      std::uint64_t editable_surface_index_upload_count = 0u;
      std::uint64_t editable_surface_palette_upload_count = 0u;
      std::uint64_t editable_surface_uploaded_vertex_bytes = 0u;
      std::uint64_t editable_surface_uploaded_index_bytes = 0u;
    };

    [[nodiscard]] GeometryCacheStats geometry_cache_stats() const;

  private:
    struct GeometryCacheState;
    bool acquire_geometry(const RenderMeshAssetSnapshot &mesh,
                          MeshRenderableProxy &proxy);
    bool acquire_collision_geometry(
        std::shared_ptr<const MeshCollisionGeometryPayload> geometry,
        MeshRenderableProxy &proxy);
    bool initialize_instance_overlay_fill(
        MeshRenderableProxy &proxy, filament::Material &material,
        std::span<const QMat4> world_transforms,
        const RenderInstanceBounds &world_bounds,
        const ViewportSettingColor &color, std::uint16_t blend_order);
    bool acquire_dynamic_geometry(const RenderMeshAssetSnapshot &mesh,
                                  MeshRenderableProxy &proxy,
                                  std::span<const QVec3> dynamic_positions);
    void release_geometry(MeshRenderableProxy &proxy);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MaterialBindingService *material_binding_ = nullptr;
    std::unique_ptr<GeometryCacheState> geometry_cache_;
    std::size_t pending_resource_retirements_ = 0u;
};

} // namespace quader

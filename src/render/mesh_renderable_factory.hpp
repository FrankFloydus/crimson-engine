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
#include "render/render_layers.hpp"
#include "render/scene_render_extraction.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <cstdint>

namespace quader {

class MaterialBindingService;
class ViewportSettings;

/**
 * Creates Mesh Renderable Factory resources for the render extraction and Filament resource layer.
 */
class MeshRenderableFactory {
public:
    MeshRenderableFactory() = default;
    MeshRenderableFactory(const MeshRenderableFactory&) = delete;
    MeshRenderableFactory& operator=(const MeshRenderableFactory&) = delete;
    MeshRenderableFactory(MeshRenderableFactory&&) = delete;
    MeshRenderableFactory& operator=(MeshRenderableFactory&&) = delete;

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
           std::uint8_t channel = kViewportDefaultRenderableChannel);
    MeshRenderableProxy create_shared_instance(
        EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
        const MeshRenderableProxy &prototype, const QMat4 &world_transform,
        std::uint8_t layer_mask = kViewportRenderedSceneLayerMask,
        std::uint8_t channel = kViewportDefaultRenderableChannel);
    void update_transform(const MeshRenderableProxy &proxy,
                          const QMat4 &world_transform);
    void update_surface_grid(const MeshRenderableProxy &proxy,
                             float grid_world_spacing,
                             const ViewportSettings &settings);
    void remove_from_scene(const MeshRenderableProxy &proxy);
    void destroy(MeshRenderableProxy& proxy);
    void destroy_detached(MeshRenderableProxy &proxy);

    static std::uint64_t mesh_asset_signature(std::uint64_t hash,
                                              const MeshAsset &mesh);

  private:
    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MaterialBindingService *material_binding_ = nullptr;
};

} // namespace quader

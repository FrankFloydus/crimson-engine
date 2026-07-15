////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "render/light_renderable_cache.hpp"
#include "render/scene_render_extraction.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <cstdint>

namespace quader {

class AssetRegistry;
class MaterialBindingService;
class MeshRenderableCache;
class InstanceRenderableCache;
class SceneDocument;

/**
 * Coordinates Scene Render Sync Service responsibilities for the render extraction and Filament resource layer.
 */
class SceneRenderSyncService {
public:
    SceneRenderSyncService() = default;
    SceneRenderSyncService(const SceneRenderSyncService&) = delete;
    SceneRenderSyncService& operator=(const SceneRenderSyncService&) = delete;
    SceneRenderSyncService(SceneRenderSyncService&&) = delete;
    SceneRenderSyncService& operator=(SceneRenderSyncService&&) = delete;
    ~SceneRenderSyncService();

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    MeshRenderableCache &mesh_cache,
                    InstanceRenderableCache &instance_cache,
                    MaterialBindingService &material_binding);
    void shutdown();

    bool initialized() const {
      return engine_ != nullptr && scene_ != nullptr &&
             mesh_cache_ != nullptr && instance_cache_ != nullptr &&
             material_binding_ != nullptr;
    }

    void bind_scene(RenderSceneBindingKey scene_binding);
    bool sync(const RenderSceneSnapshot &snapshot);
    bool render_asset_revisions_changed(
        const RenderSceneSnapshot &snapshot) const;

  private:
    [[nodiscard]] bool
    can_sync_transforms_only(const RenderSceneSnapshot &snapshot) const;
    void sync_dirty_transforms(const RenderSceneSnapshot &snapshot);
    void rebuild(const RenderSceneSnapshot &snapshot);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MeshRenderableCache *mesh_cache_ = nullptr;
    InstanceRenderableCache *instance_cache_ = nullptr;
    MaterialBindingService *material_binding_ = nullptr;
    std::uint64_t synced_revision_ = 0;
    std::uint64_t synced_asset_revision_ = 0;
    std::uint64_t synced_project_material_revision_ = 0;
    RenderInstanceEditScope synced_instance_edit_scope_;
    RenderSceneBindingKey synced_scene_binding_;
    bool has_scene_binding_ = false;
    bool has_synced_ = false;
    LightRenderableCache light_cache_;
};

} // namespace quader

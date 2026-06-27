////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "render/light_renderable_cache.hpp"
#include "scene/scene_document.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <cstdint>

namespace quader {

class AssetRegistry;
class MaterialBindingService;
class MeshRenderableCache;

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

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            MeshRenderableCache& meshCache,
            MaterialBindingService& materialBinding);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr && meshCache_ != nullptr && materialBinding_ != nullptr;
    }

    bool sync(const SceneDocument& document, const AssetRegistry& assets);
    bool renderAssetRevisionsChanged(const SceneDocument& document, const AssetRegistry& assets) const;

private:
    void rebuild(const SceneDocument& document, const AssetRegistry& assets);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MeshRenderableCache* meshCache_ = nullptr;
    MaterialBindingService* materialBinding_ = nullptr;
    std::uint64_t syncedRevision_ = 0;
    std::uint64_t syncedAssetRevision_ = 0;
    bool hasSynced_ = false;
    LightRenderableCache lightCache_;
};

} // namespace quader

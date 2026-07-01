////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_sync_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/scene_render_extraction.hpp"

namespace quader {
namespace {

constexpr std::size_t kRetiredMeshProxyDestroyBudgetPerSync = 1u;

} // namespace

SceneRenderSyncService::~SceneRenderSyncService() {
    shutdown();
}

void SceneRenderSyncService::initialize(filament::Engine& engine,
        filament::Scene& scene,
        MeshRenderableCache& meshCache,
        MaterialBindingService& materialBinding) {
    engine_ = &engine;
    scene_ = &scene;
    meshCache_ = &meshCache;
    materialBinding_ = &materialBinding;
    lightCache_.initialize(engine, scene);
}

void SceneRenderSyncService::shutdown() {
    if (!engine_) {
        return;
    }

    lightCache_.shutdown();
    syncedRevision_ = 0;
    syncedAssetRevision_ = 0;
    hasSynced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    meshCache_ = nullptr;
    materialBinding_ = nullptr;
}

bool SceneRenderSyncService::sync(const SceneDocument& document, const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return false;
    }
    {
        QDR_PROFILE_SCOPE("Scene render drain retired mesh proxies");
        meshCache_->drainRetiredDestroyQueue(kRetiredMeshProxyDestroyBudgetPerSync);
    }

    const bool documentUnchanged = hasSynced_ && syncedRevision_ == document.revision();
    const bool assetsUnchanged = hasSynced_ && syncedAssetRevision_ == assets.revision();
    if (documentUnchanged && assetsUnchanged) {
        return false;
    }
    if (documentUnchanged && !renderAssetRevisionsChanged(document, assets)) {
        syncedAssetRevision_ = assets.revision();
        return false;
    }

    {
        QDR_PROFILE_SCOPE("Scene render rebuild");
        rebuild(document, assets);
    }
    syncedRevision_ = document.revision();
    syncedAssetRevision_ = assets.revision();
    hasSynced_ = true;
    return true;
}

bool SceneRenderSyncService::renderAssetRevisionsChanged(const SceneDocument& document, const AssetRegistry& assets) const {
    QDR_PROFILE_FUNCTION();
    const SceneRenderExtractionPlan plan = [&]() {
        QDR_PROFILE_SCOPE("Scene render revision extraction plan");
        return buildSceneRenderExtractionPlan(document, assets, *materialBinding_);
    }();
    for (const SceneRenderMeshEntry& meshEntry : plan.meshes) {
        if (!meshEntry.renderable()) {
            if (meshCache_->contains(meshEntry.entityId)) {
                return true;
            }
            continue;
        }

        if (meshCache_->needsRebuild(
                    meshEntry.entityId,
                    meshEntry.meshId,
                    *meshEntry.mesh,
                    meshEntry.materialId,
                    meshEntry.castShadows(),
                    meshEntry.doubleSided(),
                    meshEntry.materialRevision)) {
            return true;
        }
    }
    return false;
}

void SceneRenderSyncService::rebuild(const SceneDocument& document, const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    const SceneRenderExtractionPlan plan = [&]() {
        QDR_PROFILE_SCOPE("Scene render extraction plan");
        return buildSceneRenderExtractionPlan(document, assets, *materialBinding_);
    }();

    for (const SceneRenderMeshEntry& meshEntry : plan.meshes) {
        QDR_PROFILE_SCOPE("Scene render sync mesh entry");
        if (!meshEntry.renderable()) {
            meshCache_->remove(meshEntry.entityId);
            LOG_WARN("Skipping render entity {} because its mesh asset is missing or empty", meshEntry.entityId);
            continue;
        }

        meshCache_->syncMesh(meshEntry.entityId,
                meshEntry.meshId,
                *meshEntry.mesh,
                meshEntry.materialId,
                meshEntry.materialComponent,
                assets,
                meshEntry.worldTransform,
                meshEntry.castShadows(),
                meshEntry.doubleSided(),
                meshEntry.materialRevision);
    }

    for (const SceneRenderLightEntry& lightEntry : plan.lights) {
        QDR_PROFILE_SCOPE("Scene render sync light entry");
        lightCache_.syncLight(lightEntry.entityId, *lightEntry.light, lightEntry.worldTransform);
    }

    {
        QDR_PROFILE_SCOPE("Scene render remove stale meshes");
        meshCache_->removeExcept(plan.liveMeshEntities);
    }
    {
        QDR_PROFILE_SCOPE("Scene render remove stale lights");
        lightCache_.removeExcept(plan.liveLightEntities);
    }
}

} // namespace quader

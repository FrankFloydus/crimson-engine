////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/SceneRenderSyncService.h"

#include "assets/AssetRegistry.h"
#include "core/Logger.h"
#include "render/MaterialBindingService.h"
#include "render/MeshRenderableCache.h"

#include <unordered_set>

namespace quader {

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
    if (!initialized()) {
        return false;
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

    rebuild(document, assets);
    syncedRevision_ = document.revision();
    syncedAssetRevision_ = assets.revision();
    hasSynced_ = true;
    return true;
}

bool SceneRenderSyncService::renderAssetRevisionsChanged(const SceneDocument& document, const AssetRegistry& assets) const {
    for (const EntityId entityId : document.entities()) {
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }
        const MeshComponent* meshComponent = document.mesh(entityId);
        if (!meshComponent) {
            continue;
        }
        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
            if (meshCache_->contains(entityId)) {
                return true;
            }
            continue;
        }

        const MaterialComponent* materialComponent = document.material(entityId);
        const AssetId materialId = materialComponent ? materialComponent->material : InvalidAssetId;
        const MaterialDocument* material = materialComponent ? assets.material(materialComponent->material) : nullptr;
        const std::uint64_t materialRevision = materialBinding_->materialRevision(assets, material);
        if (meshCache_->needsRebuild(
                    entityId,
                    meshComponent->mesh,
                    *mesh,
                    materialId,
                    meshComponent->castShadows,
                    meshComponent->doubleSided,
                    materialRevision)) {
            return true;
        }
    }
    return false;
}

void SceneRenderSyncService::rebuild(const SceneDocument& document, const AssetRegistry& assets) {
    std::unordered_set<EntityId> liveMeshEntities;
    std::unordered_set<EntityId> liveLightEntities;

    for (const EntityId entityId : document.entities()) {
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }

        const Mat4 worldTransform = document.worldTransformMatrix(entityId);

        if (const auto* meshComponent = document.mesh(entityId)) {
            const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                meshCache_->remove(entityId);
                LOG_WARN("Skipping render entity {} because its mesh asset is missing or empty", entityId);
            } else {
                liveMeshEntities.insert(entityId);
                const MaterialComponent* materialComponent = document.material(entityId);
                const AssetId materialId = materialComponent ? materialComponent->material : InvalidAssetId;
                const MaterialDocument* material = materialComponent ? assets.material(materialComponent->material) : nullptr;
                const std::uint64_t materialRevision = materialBinding_->materialRevision(assets, material);
                meshCache_->syncMesh(entityId,
                        meshComponent->mesh,
                        *mesh,
                        materialId,
                        material,
                        assets,
                        worldTransform,
                        meshComponent->castShadows,
                        meshComponent->doubleSided,
                        materialRevision);
            }
        }

        if (const auto* light = document.light(entityId)) {
            liveLightEntities.insert(entityId);
            lightCache_.syncLight(entityId, *light, worldTransform);
        }
    }

    meshCache_->removeExcept(liveMeshEntities);
    lightCache_.removeExcept(liveLightEntities);
}

} // namespace quader

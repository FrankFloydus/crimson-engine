////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/render_layers.hpp"

#include <utility>

namespace quader {
namespace {

bool meshProxyNeedsRebuild(
        const std::unordered_map<EntityId, MeshRenderableProxy>& proxies,
        EntityId entityId,
        AssetId meshId,
        const MeshAsset& mesh,
        AssetId materialId,
        bool castShadows,
        bool doubleSided,
        std::uint64_t materialRevision) {
    const auto existing = proxies.find(entityId);
    return existing == proxies.end() ||
            existing->second.mesh != meshId ||
            existing->second.meshRevision != mesh.revision.value ||
            existing->second.material != materialId ||
            existing->second.castShadows != castShadows ||
            existing->second.doubleSided != doubleSided ||
            existing->second.materialRevision != materialRevision;
}

void destroyProxyMap(MeshRenderableFactory& factory, std::unordered_map<EntityId, MeshRenderableProxy>& proxies) {
    for (auto& [id, proxy] : proxies) {
        factory.destroy(proxy);
    }
    proxies.clear();
}

} // namespace

MeshRenderableCache::~MeshRenderableCache() {
    shutdown();
}

void MeshRenderableCache::initialize(MeshRenderableFactory& factory) {
    factory_ = &factory;
}

void MeshRenderableCache::shutdown() {
    if (!factory_) {
        return;
    }

    destroyProxyMap(*factory_, renderedMeshProxies_);
    destroyProxyMap(*factory_, shadedMeshProxies_);
    destroyProxyMap(*factory_, overlayDepthMeshProxies_);
    factory_ = nullptr;
}

bool MeshRenderableCache::contains(EntityId entityId) const {
    return renderedMeshProxies_.contains(entityId) ||
            shadedMeshProxies_.contains(entityId) ||
            overlayDepthMeshProxies_.contains(entityId);
}

bool MeshRenderableCache::needsRebuild(EntityId entityId,
        AssetId meshId,
        const MeshAsset& mesh,
        AssetId materialId,
        bool castShadows,
        bool doubleSided,
        std::uint64_t materialRevision) const {
    return meshProxyNeedsRebuild(
                    renderedMeshProxies_,
                    entityId,
                    meshId,
                    mesh,
                    materialId,
                    castShadows,
                    doubleSided,
                    materialRevision) ||
            meshProxyNeedsRebuild(
                    shadedMeshProxies_,
                    entityId,
                    meshId,
                    mesh,
                    materialId,
                    false,
                    doubleSided,
                    materialRevision) ||
            meshProxyNeedsRebuild(
                    overlayDepthMeshProxies_,
                    entityId,
                    meshId,
                    mesh,
                    materialId,
                    false,
                    doubleSided,
                    materialRevision);
}

void MeshRenderableCache::syncMesh(EntityId entityId,
        AssetId meshId,
        const MeshAsset& mesh,
        AssetId materialId,
        const MaterialComponent* material,
        const AssetRegistry& assets,
        const QMat4& worldTransform,
        bool castShadows,
        bool doubleSided,
        std::uint64_t materialRevision) {
    QDR_PROFILE_FUNCTION();
    if (!factory_) {
        return;
    }

    if (needsRebuild(entityId, meshId, mesh, materialId, castShadows, doubleSided, materialRevision)) {
        QDR_PROFILE_SCOPE("Mesh renderable rebuild proxies");
        remove(entityId);
        MeshRenderableProxy renderedProxy = factory_->create(
                entityId,
                meshId,
                mesh,
                material,
                assets,
                worldTransform,
                castShadows,
                doubleSided,
                materialRevision,
                kViewportRenderedSceneLayerMask,
                false);
        renderedMeshProxies_.emplace(entityId, std::move(renderedProxy));
        MeshRenderableProxy shadedProxy = factory_->create(
                entityId,
                meshId,
                mesh,
                material,
                assets,
                worldTransform,
                false,
                doubleSided,
                materialRevision,
                kViewportShadedSceneLayerMask,
                true);
        shadedMeshProxies_.emplace(entityId, std::move(shadedProxy));
        MeshRenderableProxy overlayDepthProxy = factory_->create(
                entityId,
                meshId,
                mesh,
                material,
                assets,
                worldTransform,
                false,
                doubleSided,
                materialRevision,
                kViewportOverlayLayerMask,
                true,
                true,
                kViewportOverlayDepthChannel);
        overlayDepthMeshProxies_.emplace(entityId, std::move(overlayDepthProxy));
        return;
    }

    if (auto existing = renderedMeshProxies_.find(entityId); existing != renderedMeshProxies_.end()) {
        QDR_PROFILE_SCOPE("Mesh renderable update rendered transform");
        factory_->updateTransform(existing->second, worldTransform);
    }
    if (auto existing = shadedMeshProxies_.find(entityId); existing != shadedMeshProxies_.end()) {
        QDR_PROFILE_SCOPE("Mesh renderable update shaded transform");
        factory_->updateTransform(existing->second, worldTransform);
    }
    if (auto existing = overlayDepthMeshProxies_.find(entityId); existing != overlayDepthMeshProxies_.end()) {
        QDR_PROFILE_SCOPE("Mesh renderable update depth transform");
        factory_->updateTransform(existing->second, worldTransform);
    }
}

void MeshRenderableCache::remove(EntityId entityId) {
    QDR_PROFILE_FUNCTION();
    if (!factory_) {
        return;
    }

    if (const auto existing = renderedMeshProxies_.find(entityId); existing != renderedMeshProxies_.end()) {
        factory_->destroy(existing->second);
        renderedMeshProxies_.erase(existing);
    }
    if (const auto existing = shadedMeshProxies_.find(entityId); existing != shadedMeshProxies_.end()) {
        factory_->destroy(existing->second);
        shadedMeshProxies_.erase(existing);
    }
    if (const auto existing = overlayDepthMeshProxies_.find(entityId); existing != overlayDepthMeshProxies_.end()) {
        factory_->destroy(existing->second);
        overlayDepthMeshProxies_.erase(existing);
    }
}

void MeshRenderableCache::removeExcept(const std::unordered_set<EntityId>& liveMeshEntities) {
    QDR_PROFILE_FUNCTION();
    if (!factory_) {
        return;
    }

    for (auto it = renderedMeshProxies_.begin(); it != renderedMeshProxies_.end();) {
        if (!liveMeshEntities.contains(it->first)) {
            factory_->destroy(it->second);
            it = renderedMeshProxies_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = shadedMeshProxies_.begin(); it != shadedMeshProxies_.end();) {
        if (!liveMeshEntities.contains(it->first)) {
            factory_->destroy(it->second);
            it = shadedMeshProxies_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = overlayDepthMeshProxies_.begin(); it != overlayDepthMeshProxies_.end();) {
        if (!liveMeshEntities.contains(it->first)) {
            factory_->destroy(it->second);
            it = overlayDepthMeshProxies_.erase(it);
        } else {
            ++it;
        }
    }
}

std::uint64_t MeshRenderableCache::meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh) {
    return MeshRenderableFactory::meshAssetSignature(hash, mesh);
}

} // namespace quader

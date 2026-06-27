////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_cache.hpp"

#include <utility>

namespace quader {

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

    for (auto& [id, proxy] : meshProxies_) {
        factory_->destroy(proxy);
    }
    meshProxies_.clear();
    factory_ = nullptr;
}

bool MeshRenderableCache::contains(EntityId entityId) const {
    return meshProxies_.contains(entityId);
}

bool MeshRenderableCache::needsRebuild(EntityId entityId,
        AssetId meshId,
        const MeshAsset& mesh,
        AssetId materialId,
        bool castShadows,
        bool doubleSided,
        std::uint64_t materialRevision) const {
    const auto existing = meshProxies_.find(entityId);
    return existing == meshProxies_.end() ||
            existing->second.mesh != meshId ||
            existing->second.meshRevision != mesh.revision.value ||
            existing->second.material != materialId ||
            existing->second.castShadows != castShadows ||
            existing->second.doubleSided != doubleSided ||
            existing->second.materialRevision != materialRevision;
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
    if (!factory_) {
        return;
    }

    if (needsRebuild(entityId, meshId, mesh, materialId, castShadows, doubleSided, materialRevision)) {
        remove(entityId);
        MeshRenderableProxy proxy = factory_->create(
                entityId,
                meshId,
                mesh,
                material,
                assets,
                worldTransform,
                castShadows,
                doubleSided,
                materialRevision);
        meshProxies_.emplace(entityId, std::move(proxy));
        return;
    }

    auto existing = meshProxies_.find(entityId);
    if (existing != meshProxies_.end()) {
        factory_->updateTransform(existing->second, worldTransform);
    }
}

void MeshRenderableCache::remove(EntityId entityId) {
    if (!factory_) {
        return;
    }

    const auto existing = meshProxies_.find(entityId);
    if (existing == meshProxies_.end()) {
        return;
    }
    factory_->destroy(existing->second);
    meshProxies_.erase(existing);
}

void MeshRenderableCache::removeExcept(const std::unordered_set<EntityId>& liveMeshEntities) {
    if (!factory_) {
        return;
    }

    for (auto it = meshProxies_.begin(); it != meshProxies_.end();) {
        if (!liveMeshEntities.contains(it->first)) {
            factory_->destroy(it->second);
            it = meshProxies_.erase(it);
        } else {
            ++it;
        }
    }
}

std::uint64_t MeshRenderableCache::meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh) {
    return MeshRenderableFactory::meshAssetSignature(hash, mesh);
}

} // namespace quader

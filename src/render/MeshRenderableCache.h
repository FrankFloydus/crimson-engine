////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "math/Types.h"
#include "render/MeshRenderableFactory.h"
#include "render/MeshRenderableProxy.h"
#include "scene/SceneDocument.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace quader {

/**
 * Caches Mesh Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class MeshRenderableCache {
public:
    using MeshProxy = MeshRenderableProxy;

    ~MeshRenderableCache();

    void initialize(MeshRenderableFactory& factory);
    void shutdown();

    bool initialized() const {
        return factory_ != nullptr && factory_->initialized();
    }

    bool contains(EntityId entityId) const;
    bool needsRebuild(EntityId entityId,
            AssetId meshId,
            const MeshAsset& mesh,
            AssetId materialId,
            bool castShadows,
            bool doubleSided,
            std::uint64_t materialRevision) const;

    void syncMesh(EntityId entityId,
            AssetId meshId,
            const MeshAsset& mesh,
            AssetId materialId,
            const MaterialDocument* material,
            const AssetRegistry& assets,
            const Mat4& worldTransform,
            bool castShadows,
            bool doubleSided,
            std::uint64_t materialRevision);
    void remove(EntityId entityId);
    void removeExcept(const std::unordered_set<EntityId>& liveMeshEntities);

    static std::uint64_t meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh);

private:
    MeshRenderableFactory* factory_ = nullptr;
    std::unordered_map<EntityId, MeshRenderableProxy> meshProxies_;
};

} // namespace quader

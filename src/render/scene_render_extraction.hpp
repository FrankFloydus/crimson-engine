////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "scene/components.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace quader {

class AssetRegistry;
class MaterialBindingService;
class MeshAsset;
class SceneDocument;

/**
 * Describes one scene mesh extracted for render sync.
 */
struct SceneRenderMeshEntry {
    EntityId entityId = InvalidEntityId;
    AssetId meshId = InvalidAssetId;
    const MeshAsset* mesh = nullptr;
    const MeshComponent* meshComponent = nullptr;
    const MaterialComponent* materialComponent = nullptr;
    AssetId materialId = InvalidAssetId;
    std::uint64_t materialRevision = 0;
    QMat4 worldTransform;

    [[nodiscard]] bool renderable() const;
    [[nodiscard]] bool castShadows() const;
    [[nodiscard]] bool doubleSided() const;
};

/**
 * Describes one scene light extracted for render sync.
 */
struct SceneRenderLightEntry {
    EntityId entityId = InvalidEntityId;
    const LightComponent* light = nullptr;
    QMat4 worldTransform;
};

/**
 * Shared render extraction plan consumed by cache invalidation and rebuild.
 */
struct SceneRenderExtractionPlan {
    std::vector<SceneRenderMeshEntry> meshes;
    std::vector<SceneRenderLightEntry> lights;
    std::unordered_set<EntityId> liveMeshEntities;
    std::unordered_set<EntityId> liveLightEntities;
};

[[nodiscard]] SceneRenderExtractionPlan buildSceneRenderExtractionPlan(
        const SceneDocument& document,
        const AssetRegistry& assets,
        const MaterialBindingService& materialBinding);

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/SceneBounds.h"

#include "math/GlmBridge.h"

#include <algorithm>
#include <array>

namespace quader {
namespace {

std::array<QVec3, 8> boundsCorners(const SceneBounds& bounds) {
    const QVec3& min = bounds.min;
    const QVec3& max = bounds.max;
    return {{
            {min.x, min.y, min.z},
            {max.x, min.y, min.z},
            {max.x, max.y, min.z},
            {min.x, max.y, min.z},
            {min.x, min.y, max.z},
            {max.x, min.y, max.z},
            {max.x, max.y, max.z},
            {min.x, max.y, max.z},
    }};
}

void includeBounds(std::optional<SceneBounds>& result, const SceneBounds& bounds) {
    for (const QVec3 corner : boundsCorners(bounds)) {
        if (!result) {
            result = SceneBounds{.min = corner, .max = corner};
        } else {
            includePoint(*result, corner);
        }
    }
}

} // namespace

std::optional<SceneBounds> localMeshBounds(const MeshAsset& mesh) {
    if (mesh.vertices.empty()) {
        return std::nullopt;
    }

    SceneBounds bounds{
            .min = mesh.vertices.front().position,
            .max = mesh.vertices.front().position,
    };
    for (const MeshVertex& vertex : mesh.vertices) {
        includePoint(bounds, vertex.position);
    }
    return bounds;
}

void includePoint(SceneBounds& bounds, QVec3 point) {
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

QVec3 boundsCenter(const SceneBounds& bounds) {
    return (bounds.min + bounds.max) * 0.5f;
}

std::optional<SceneBounds> entityWorldBounds(const SceneDocument& document, const AssetRegistry& assets, EntityId entity) {
    if (!document.isEffectivelyVisible(entity)) {
        return std::nullopt;
    }

    const TransformComponent* transform = document.transform(entity);
    if (!transform) {
        return std::nullopt;
    }

    const QMat4 worldFromLocal = document.worldTransformMatrix(entity);
    const MeshComponent* meshComponent = document.mesh(entity);
    if (meshComponent) {
        if (const MeshAsset* mesh = assets.mesh(meshComponent->mesh)) {
            if (const std::optional<SceneBounds> localBounds = localMeshBounds(*mesh)) {
                std::optional<SceneBounds> result;
                for (const QVec3 localCorner : boundsCorners(*localBounds)) {
                    const QVec3 worldCorner = transformPoint(worldFromLocal, localCorner);
                    if (!result) {
                        result = SceneBounds{.min = worldCorner, .max = worldCorner};
                    } else {
                        includePoint(*result, worldCorner);
                    }
                }
                return result;
            }
        }
    }

    const QVec3 worldPosition = transformPoint(worldFromLocal, {});
    return SceneBounds{.min = worldPosition, .max = worldPosition};
}

std::optional<SceneBounds> entityHierarchyWorldBounds(const SceneDocument& document, const AssetRegistry& assets, EntityId entity) {
    if (!document.exists(entity) || !document.isEffectivelyVisible(entity)) {
        return std::nullopt;
    }

    std::optional<SceneBounds> result;
    for (const EntityId descendant : document.descendants(entity, true)) {
        if (descendant != entity && !document.isEffectivelyVisible(descendant)) {
            continue;
        }
        if (const MeshComponent* mesh = document.mesh(descendant)) {
            (void)mesh;
            if (const std::optional<SceneBounds> bounds = entityWorldBounds(document, assets, descendant)) {
                includeBounds(result, *bounds);
            }
        }
    }

    if (!result) {
        return entityWorldBounds(document, assets, entity);
    }
    return result;
}

std::optional<SceneBounds> documentWorldBounds(const SceneDocument& document, const AssetRegistry& assets) {
    std::optional<SceneBounds> result;
    for (const EntityId entity : document.entities()) {
        if (const std::optional<SceneBounds> bounds = entityWorldBounds(document, assets, entity)) {
            includeBounds(result, *bounds);
        }
    }
    return result;
}

std::optional<SceneBounds> selectedWorldBounds(const SceneDocument& document, const AssetRegistry& assets) {
    std::optional<SceneBounds> result;
    for (const EntityId entity : document.selectedEntities()) {
        if (const std::optional<SceneBounds> bounds = entityWorldBounds(document, assets, entity)) {
            includeBounds(result, *bounds);
        }
    }
    return result;
}

std::optional<SceneBounds> selectedTransformWorldBounds(const SceneDocument& document, const AssetRegistry& assets) {
    std::optional<SceneBounds> result;
    for (const EntityId entity : document.selectedEntities()) {
        const std::optional<SceneBounds> bounds = document.hasChildren(entity)
                ? entityHierarchyWorldBounds(document, assets, entity)
                : entityWorldBounds(document, assets, entity);
        if (bounds) {
            includeBounds(result, *bounds);
        }
    }
    return result;
}

} // namespace quader

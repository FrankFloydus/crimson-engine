#include "scene/SceneBounds.h"

#include <algorithm>
#include <array>

namespace quader {
namespace {

std::array<Vec3, 8> boundsCorners(const SceneBounds& bounds) {
    const Vec3& min = bounds.min;
    const Vec3& max = bounds.max;
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
    for (const Vec3 corner : boundsCorners(bounds)) {
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

void includePoint(SceneBounds& bounds, Vec3 point) {
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

Vec3 boundsCenter(const SceneBounds& bounds) {
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

    const Mat4 worldFromLocal = document.worldTransformMatrix(entity);
    const MeshComponent* meshComponent = document.mesh(entity);
    if (meshComponent) {
        if (const MeshAsset* mesh = assets.mesh(meshComponent->mesh)) {
            if (const std::optional<SceneBounds> localBounds = localMeshBounds(*mesh)) {
                std::optional<SceneBounds> result;
                for (const Vec3 localCorner : boundsCorners(*localBounds)) {
                    const Vec3 worldCorner = transformPoint(worldFromLocal, localCorner);
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

    const Vec3 worldPosition = transformPoint(worldFromLocal, {});
    return SceneBounds{.min = worldPosition, .max = worldPosition};
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

} // namespace quader

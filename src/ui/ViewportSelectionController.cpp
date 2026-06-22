#include "ui/ViewportSelectionController.h"

#include "assets/MeshAsset.h"
#include "scene/SceneBounds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace quader {
namespace {

ViewportSelectionMode toViewportSelectionMode(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return ViewportSelectionMode::Vertex;
    case ModelingSelectionMode::Edge:
        return ViewportSelectionMode::Edge;
    case ModelingSelectionMode::Face:
        return ViewportSelectionMode::Face;
    case ModelingSelectionMode::Object:
        return ViewportSelectionMode::Object;
    case ModelingSelectionMode::Group:
        return ViewportSelectionMode::Group;
    }
    return ViewportSelectionMode::Object;
}

bool rayIntersectsBounds(Vec3 origin, Vec3 direction, const SceneBounds& bounds, float& outDistance) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    auto intersectAxis = [&](float originAxis, float directionAxis, float minAxis, float maxAxis) {
        if (std::abs(directionAxis) <= 0.000001f) {
            return originAxis >= minAxis && originAxis <= maxAxis;
        }

        float t1 = (minAxis - originAxis) / directionAxis;
        float t2 = (maxAxis - originAxis) / directionAxis;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!intersectAxis(origin.x, direction.x, bounds.min.x, bounds.max.x)) {
        return false;
    }
    if (!intersectAxis(origin.y, direction.y, bounds.min.y, bounds.max.y)) {
        return false;
    }
    if (!intersectAxis(origin.z, direction.z, bounds.min.z, bounds.max.z)) {
        return false;
    }

    outDistance = tMin;
    return tMax >= 0.0f;
}

EntityId nearestGroupAncestor(const SceneDocument& document, EntityId entity) {
    EntityId parent = document.parent(entity);
    while (parent != InvalidEntityId) {
        if (document.kind(parent) == EntityKind::Empty) {
            return parent;
        }
        parent = document.parent(parent);
    }
    return InvalidEntityId;
}

EntityId pickMeshEntityByBounds(
        const SceneDocument& document,
        const AssetRegistry& assets,
        const ViewportRay& worldRay) {
    EntityId bestEntity = InvalidEntityId;
    float bestDistance = std::numeric_limits<float>::max();

    for (const EntityId id : document.entities()) {
        const VisibilityComponent* visibility = document.visibility(id);
        if (!document.isEffectivelyVisible(id) ||
                (visibility != nullptr && !visibility->selectable)) {
            continue;
        }

        const MeshComponent* meshComponent = document.mesh(id);
        if (!meshComponent || document.transform(id) == nullptr) {
            continue;
        }

        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (!mesh) {
            continue;
        }

        const std::optional<SceneBounds> bounds = localMeshBounds(*mesh);
        if (!bounds) {
            continue;
        }

        const Mat4 worldFromLocal = document.worldTransformMatrix(id);
        const Mat4 localFromWorld = inverse(worldFromLocal);
        const Vec3 localOrigin = transformPoint(localFromWorld, worldRay.origin);
        const Vec3 unnormalizedLocalDirection = transformVector(localFromWorld, worldRay.direction);
        if (length(unnormalizedLocalDirection) <= 0.000001f) {
            continue;
        }
        const Vec3 localDirection = normalize(unnormalizedLocalDirection);

        float localDistance = 0.0f;
        if (!rayIntersectsBounds(localOrigin, localDirection, *bounds, localDistance)) {
            continue;
        }

        const Vec3 localHit = localOrigin + localDirection * localDistance;
        const Vec3 worldHit = transformPoint(worldFromLocal, localHit);
        const float worldDistance = length(worldHit - worldRay.origin);
        if (worldDistance < bestDistance) {
            bestDistance = worldDistance;
            bestEntity = id;
        }
    }

    return bestEntity;
}

ModelingSelectionSummary applyScenePickSelection(
        SceneDocument& document,
        DocumentCommandService& documentCommands,
        ModelingSessionService& modeling,
        ViewportInputRouter& inputRouter,
        EntityId targetEntity,
        ModelingSelectionMode mode,
        ViewportModifiers modifiers,
        bool preserveSelectionOnMiss,
        bool preserveSelectionOnSelectedHit) {
    const bool targetSelected =
            targetEntity != InvalidEntityId &&
            document.selection(targetEntity) != nullptr &&
            document.selection(targetEntity)->selected;
    const bool preserveSelectedHit = preserveSelectionOnSelectedHit && targetSelected;
    const std::vector<EntityId> previousSelected = document.selectedEntities();
    std::vector<EntityId> selected = previousSelected;
    if (!modifiers.primary &&
            !modifiers.shift &&
            !preserveSelectedHit &&
            (!preserveSelectionOnMiss || targetEntity != InvalidEntityId)) {
        selected.clear();
    }
    if (targetEntity != InvalidEntityId && !preserveSelectedHit) {
        const auto found = std::find(selected.begin(), selected.end(), targetEntity);
        const bool alreadySelected = found != selected.end();
        if (modifiers.primary && alreadySelected) {
            selected.erase(found);
        } else if (!alreadySelected) {
            selected.push_back(targetEntity);
        }
    }
    if (selected != previousSelected) {
        const EntityId active = selected.empty() ? InvalidEntityId :
                (targetEntity != InvalidEntityId && std::find(selected.begin(), selected.end(), targetEntity) != selected.end()
                                ? targetEntity
                                : selected.back());
        (void)documentCommands.selectEntities(std::move(selected), active);
    }
    const ModelingSelectionSummary summary = modeling.selectionSummary();
    inputRouter.setSelectionMode(toViewportSelectionMode(summary.mode == mode ? summary.mode : mode));
    return summary.mode == mode ? summary : ModelingSelectionSummary{
            .mode = mode,
            .hasSelection = !document.selectedEntities().empty(),
            .activeKind = document.selectedEntities().empty() ? ModelingActiveSelectionKind::None : ModelingActiveSelectionKind::Object,
            .selectedObjectCount = static_cast<std::uint32_t>(document.selectedEntities().size()),
    };
}

} // namespace

ModelingSelectionSummary ViewportSelectionController::pickSelection(
        SceneDocument& document,
        DocumentCommandService& documentCommands,
        const AssetRegistry& assets,
        ModelingSessionService& modeling,
        ViewportInputRouter& inputRouter,
        const ViewportSettings& settings,
        const ViewportCameraController& camera,
        Vec2 localMouse,
        ViewportModifiers modifiers,
        int clickCount,
        bool preserveSelectionOnMiss,
        bool preserveSelectionOnSelectedHit) const {
    const ViewportRay worldRay = camera.rayFromViewportPoint(localMouse);
    const bool groupMode = modeling.selectionMode() == ModelingSelectionMode::Group;
    if (groupMode) {
        const EntityId hitMesh = pickMeshEntityByBounds(document, assets, worldRay);
        const EntityId targetGroup = nearestGroupAncestor(document, hitMesh);
        return applyScenePickSelection(
                document,
                documentCommands,
                modeling,
                inputRouter,
                targetGroup,
                ModelingSelectionMode::Group,
                modifiers,
                preserveSelectionOnMiss,
                preserveSelectionOnSelectedHit);
    }
    if (modeling.sdkAvailable()) {
        const ModelingSelectionSummary summary = modeling.pickSelect(ModelingPickSelectRequest{
                .ray = {
                        .origin = worldRay.origin,
                        .direction = worldRay.direction,
                },
                .clickCount = clickCount,
                .controlPressed = modifiers.primary,
                .shiftPressed = modifiers.shift,
                .commandPressed = modifiers.primary,
                .altPressed = modifiers.alt,
                .preserveSelectionOnMiss = preserveSelectionOnMiss,
                .preserveSelectionOnSelectedHit = preserveSelectionOnSelectedHit,
                .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
        });
        inputRouter.setSelectionMode(toViewportSelectionMode(summary.mode));
        return summary;
    }

    const EntityId hitMesh = pickMeshEntityByBounds(document, assets, worldRay);
    const EntityId targetEntity = groupMode ? nearestGroupAncestor(document, hitMesh) : hitMesh;
    return applyScenePickSelection(
            document,
            documentCommands,
            modeling,
            inputRouter,
            targetEntity,
            groupMode ? ModelingSelectionMode::Group : ModelingSelectionMode::Object,
            modifiers,
            preserveSelectionOnMiss,
            preserveSelectionOnSelectedHit);
}

} // namespace quader

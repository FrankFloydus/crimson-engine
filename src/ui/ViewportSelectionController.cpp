#include "ui/ViewportSelectionController.h"

#include "assets/MeshAsset.h"
#include "scene/SceneBounds.h"

#include <algorithm>
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

    const bool bestEntitySelected =
            bestEntity != InvalidEntityId &&
            document.selection(bestEntity) != nullptr &&
            document.selection(bestEntity)->selected;
    const bool preserveSelectedHit = preserveSelectionOnSelectedHit && bestEntitySelected;
    const std::vector<EntityId> previousSelected = document.selectedEntities();
    std::vector<EntityId> selected = previousSelected;
    if (!modifiers.primary &&
            !modifiers.shift &&
            !preserveSelectedHit &&
            (!preserveSelectionOnMiss || bestEntity != InvalidEntityId)) {
        selected.clear();
    }
    if (bestEntity != InvalidEntityId && !preserveSelectedHit) {
        const auto found = std::find(selected.begin(), selected.end(), bestEntity);
        const bool alreadySelected = found != selected.end();
        if (modifiers.primary && alreadySelected) {
            selected.erase(found);
        } else if (!alreadySelected) {
            selected.push_back(bestEntity);
        }
    }
    if (selected != previousSelected) {
        const EntityId active = selected.empty() ? InvalidEntityId :
                (bestEntity != InvalidEntityId && std::find(selected.begin(), selected.end(), bestEntity) != selected.end()
                                ? bestEntity
                                : selected.back());
        (void)documentCommands.selectEntities(std::move(selected), active);
    }
    const bool hasSelection = !document.selectedEntities().empty();
    return {
            .mode = ModelingSelectionMode::Object,
            .hasSelection = hasSelection,
            .activeKind = hasSelection ? ModelingActiveSelectionKind::Object : ModelingActiveSelectionKind::None,
            .selectedObjectCount = static_cast<std::uint32_t>(document.selectedEntities().size()),
    };
}

} // namespace quader

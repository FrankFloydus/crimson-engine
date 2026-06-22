////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/EditorSelectionService.h"

#include "assets/AssetRegistry.h"
#include "assets/MeshAsset.h"
#include "document/DocumentCommandService.h"
#include "modeling/services/ModelingSessionService.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace quader {
namespace {

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

DocumentCommandResult mergeModelingSelectionChange(
        DocumentCommandResult result,
        const ModelingSelectionSummary& before,
        const ModelingSelectionSummary& after) {
    if (result.ok &&
            (before.contentRevision != after.contentRevision ||
                    before.selectionRevision != after.selectionRevision)) {
        result.changed = true;
    }
    return result;
}

} // namespace

EditorSelectionService::EditorSelectionService(
        SceneDocument& scene,
        DocumentCommandService& documentCommands,
        AssetRegistry& assets,
        ModelingSessionService& modeling)
    : scene_(scene)
    , documentCommands_(documentCommands)
    , assets_(assets)
    , modeling_(modeling) {}

EditorSelectionState EditorSelectionService::state() const {
    const ModelingSelectionSummary currentSummary = modeling_.selectionSummary();
    return {
            .mode = currentSummary.mode,
            .summary = currentSummary,
            .target = targetFromSummary(currentSummary),
            .sceneSelection = scene_.selectedEntities(),
            .activeEntity = scene_.primarySelection(),
    };
}

EditorSelectionTarget EditorSelectionService::target() const {
    return targetFromSummary(modeling_.selectionSummary());
}

ModelingSelectionSummary EditorSelectionService::summary() const {
    return modeling_.selectionSummary();
}

ModelingHoverSummary EditorSelectionService::hoverSummary() const {
    return modeling_.selectionHoverSummary();
}

ModelingSelectionMode EditorSelectionService::mode() const {
    return modeling_.selectionMode();
}

DocumentCommandResult EditorSelectionService::setSceneSelection(std::span<const EntityId> entities, EntityId active) {
    const ModelingSelectionSummary before = modeling_.selectionSummary();
    DocumentCommandResult result =
            documentCommands_.selectEntities(std::vector<EntityId>(entities.begin(), entities.end()), active);
    if (result.ok && modeling_.selectionMode() != ModelingSelectionMode::Group) {
        modeling_.projectObjectSelectionFromScene();
    }
    return mergeModelingSelectionChange(result, before, modeling_.selectionSummary());
}

ModelingCommandResult EditorSelectionService::setObjectSelection(std::span<const EntityId> entities, EntityId active) {
    return modeling_.setObjectSelection(entities, active);
}

ModelingCommandResult EditorSelectionService::setComponentSelection(const ModelingComponentSelectionRequest& request) {
    return modeling_.setComponentSelection(request);
}

ModelingCommandResult EditorSelectionService::setMode(ModelingSelectionMode mode) {
    return modeling_.setSelectionMode(mode);
}

ModelingCommandResult EditorSelectionService::selectAll() {
    return modeling_.selectAll();
}

ModelingCommandResult EditorSelectionService::invert() {
    return modeling_.invertSelection();
}

ModelingCommandResult EditorSelectionService::clear() {
    return modeling_.clearSelection();
}

ModelingSelectionSummary EditorSelectionService::pickSelect(const ModelingPickSelectRequest& request) {
    if (modeling_.selectionMode() == ModelingSelectionMode::Group) {
        const EntityId hitMesh = pickMeshEntityByBounds(request.ray);
        return scenePickSelection(nearestGroupAncestor(hitMesh), ModelingSelectionMode::Group, request);
    }

    if (modeling_.runtimeAvailable()) {
        return modeling_.pickSelect(request);
    }

    return scenePickSelection(pickMeshEntityByBounds(request.ray), ModelingSelectionMode::Object, request);
}

ModelingCommandResult EditorSelectionService::updateHover(const ModelingHoverRequest& request) {
    return modeling_.updateSelectionHover(request);
}

ModelingCommandResult EditorSelectionService::setHover(const ModelingDirectHoverRequest& request) {
    return modeling_.setSelectionHover(request);
}

ModelingHoverSummary EditorSelectionService::previewHover(const ModelingHoverRequest& request) const {
    return modeling_.previewSelectionHover(request);
}

ModelingCommandResult EditorSelectionService::clearHover() {
    return modeling_.clearSelectionHover();
}

EditorSelectionTarget EditorSelectionService::targetFromSummary(const ModelingSelectionSummary& summary) const {
    auto activeSceneTarget = [&]() {
        const EntityId active = scene_.primarySelection();
        if (active == InvalidEntityId || !scene_.exists(active)) {
            return EditorSelectionTarget{};
        }
        return EditorSelectionTarget{
                .kind = scene_.kind(active) == EntityKind::Empty ? EditorSelectionTargetKind::Group : EditorSelectionTargetKind::Object,
                .entity = active,
                .modelingObjectId = scene_.modelingObject(active) != nullptr ? scene_.modelingObject(active)->objectId : InvalidModelingObjectId,
        };
    };

    if (!summary.hasSelection) {
        return {};
    }

    if (summary.mode == ModelingSelectionMode::Group) {
        EditorSelectionTarget target = activeSceneTarget();
        target.kind = target.entity == InvalidEntityId ? EditorSelectionTargetKind::None : EditorSelectionTargetKind::Group;
        return target;
    }

    const auto activeEntity = summary.activeObjectId > 0
            ? modeling_.entityForModelingObject(summary.activeObjectId)
            : std::optional<EntityId>{};
    const EntityId entity = activeEntity.value_or(scene_.primarySelection());

    switch (summary.activeKind) {
    case ModelingActiveSelectionKind::Object:
        return entity == InvalidEntityId ? activeSceneTarget() : EditorSelectionTarget{
                .kind = EditorSelectionTargetKind::Object,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
        };
    case ModelingActiveSelectionKind::Vertex:
        return {
                .kind = EditorSelectionTargetKind::Vertex,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
                .vertex = summary.activeVertexId,
        };
    case ModelingActiveSelectionKind::Edge:
        return {
                .kind = EditorSelectionTargetKind::Edge,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
                .edgeA = summary.activeEdgeA,
                .edgeB = summary.activeEdgeB,
        };
    case ModelingActiveSelectionKind::Face:
        return {
                .kind = EditorSelectionTargetKind::Face,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
                .face = summary.activeFaceId,
        };
    case ModelingActiveSelectionKind::Light:
    case ModelingActiveSelectionKind::None:
        break;
    }

    return activeSceneTarget();
}

ModelingSelectionSummary EditorSelectionService::scenePickSelection(
        EntityId targetEntity,
        ModelingSelectionMode mode,
        const ModelingPickSelectRequest& request) {
    const bool targetSelected =
            targetEntity != InvalidEntityId &&
            scene_.selection(targetEntity) != nullptr &&
            scene_.selection(targetEntity)->selected;
    const bool preserveSelectedHit = request.preserveSelectionOnSelectedHit && targetSelected;
    const std::vector<EntityId> previousSelected = scene_.selectedEntities();
    std::vector<EntityId> selected = previousSelected;

    const bool additiveToggle = request.controlPressed || request.commandPressed;
    if (!additiveToggle &&
            !request.shiftPressed &&
            !preserveSelectedHit &&
            (!request.preserveSelectionOnMiss || targetEntity != InvalidEntityId)) {
        selected.clear();
    }
    if (targetEntity != InvalidEntityId && !preserveSelectedHit) {
        const auto found = std::find(selected.begin(), selected.end(), targetEntity);
        const bool alreadySelected = found != selected.end();
        if (additiveToggle && alreadySelected) {
            selected.erase(found);
        } else if (!alreadySelected) {
            selected.push_back(targetEntity);
        }
    }

    if (selected != previousSelected) {
        const EntityId active = selected.empty() ? InvalidEntityId :
                (targetEntity != InvalidEntityId &&
                                std::find(selected.begin(), selected.end(), targetEntity) != selected.end()
                        ? targetEntity
                        : selected.back());
        (void)setSceneSelection(std::span<const EntityId>(selected.data(), selected.size()), active);
    }

    const ModelingSelectionSummary current = modeling_.selectionSummary();
    return current.mode == mode ? current : sceneSummaryFor(mode);
}

EntityId EditorSelectionService::pickMeshEntityByBounds(const ModelingRay& ray) const {
    EntityId bestEntity = InvalidEntityId;
    float bestDistance = std::numeric_limits<float>::max();

    for (const EntityId id : scene_.entities()) {
        const VisibilityComponent* visibility = scene_.visibility(id);
        if (!scene_.isEffectivelyVisible(id) ||
                (visibility != nullptr && !visibility->selectable)) {
            continue;
        }

        const MeshComponent* meshComponent = scene_.mesh(id);
        if (!meshComponent || scene_.transform(id) == nullptr) {
            continue;
        }

        const MeshAsset* mesh = assets_.mesh(meshComponent->mesh);
        if (!mesh) {
            continue;
        }

        const std::optional<SceneBounds> bounds = localMeshBounds(*mesh);
        if (!bounds) {
            continue;
        }

        const Mat4 worldFromLocal = scene_.worldTransformMatrix(id);
        const Mat4 localFromWorld = inverse(worldFromLocal);
        const Vec3 localOrigin = transformPoint(localFromWorld, ray.origin);
        const Vec3 unnormalizedLocalDirection = transformVector(localFromWorld, ray.direction);
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
        const float worldDistance = length(worldHit - ray.origin);
        if (worldDistance < bestDistance) {
            bestDistance = worldDistance;
            bestEntity = id;
        }
    }

    return bestEntity;
}

EntityId EditorSelectionService::nearestGroupAncestor(EntityId entity) const {
    EntityId parent = scene_.parent(entity);
    while (parent != InvalidEntityId) {
        if (scene_.kind(parent) == EntityKind::Empty) {
            return parent;
        }
        parent = scene_.parent(parent);
    }
    return InvalidEntityId;
}

ModelingSelectionSummary EditorSelectionService::sceneSummaryFor(ModelingSelectionMode mode) const {
    const std::vector<EntityId> selected = scene_.selectedEntities();
    return {
            .mode = mode,
            .hasSelection = !selected.empty(),
            .activeKind = selected.empty() ? ModelingActiveSelectionKind::None : ModelingActiveSelectionKind::Object,
            .selectedObjectCount = static_cast<std::uint32_t>(selected.size()),
            .contentRevision = modeling_.selectionSummary().contentRevision,
            .selectionRevision = scene_.revision(),
    };
}

} // namespace quader

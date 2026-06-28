////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/selection_projection_service.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "math/glm_bridge.hpp"
#include "scene/components.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <editor/modeling/runtime/session.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

/**
 * Stores runtime object selection projection data derived from scene entities.
 */
struct RuntimeObjectSelectionProjection {
    std::vector<int> objectIds;
    int activeObjectId = -1;
    bool hasInputEntities = false;
    bool hasModelingObjects = false;
};

template <typename Id>
Id resolveProjectedActiveSelection(
        std::span<const Id> selected,
        Id requested,
        Id invalid) {
    if (requested != invalid &&
            std::find(selected.begin(), selected.end(), requested) != selected.end()) {
        return requested;
    }
    return selected.empty() ? invalid : selected.back();
}

RuntimeObjectSelectionProjection runtimeObjectSelectionForSceneEntities(
        std::span<const EntityId> entities,
        EntityId active,
        const ModelingObjectRuntimeMap& objectRegistry) {
    RuntimeObjectSelectionProjection projection;
    projection.hasInputEntities = !entities.empty();
    projection.objectIds.reserve(entities.size());
    for (const EntityId entity : entities) {
        const auto object = objectRegistry.modelingObjectForEntity(entity);
        if (object.has_value()) {
            projection.objectIds.push_back(*object);
        }
    }
    projection.hasModelingObjects = !projection.objectIds.empty();

    if (active != InvalidEntityId) {
        const auto object = objectRegistry.modelingObjectForEntity(active);
        if (object.has_value()) {
            projection.activeObjectId = *object;
        }
    }
    projection.activeObjectId = resolveProjectedActiveSelection<int>(
            std::span<const int>(projection.objectIds.data(), projection.objectIds.size()),
            projection.activeObjectId,
            -1);
    return projection;
}

bool runtimeSelectionMatches(
        const quader_editor_core::EditorSession& session,
        const RuntimeObjectSelectionProjection& projection) {
    std::vector<int> currentObjectIds = session.selected_object_ids();
    std::sort(currentObjectIds.begin(), currentObjectIds.end());
    std::vector<int> desiredObjectIds = projection.objectIds;
    std::sort(desiredObjectIds.begin(), desiredObjectIds.end());
    return currentObjectIds == desiredObjectIds &&
            session.active_object_id() == projection.activeObjectId;
}

bool applyRuntimeObjectSelection(
        quader_editor_core::EditorSession& session,
        const RuntimeObjectSelectionProjection& projection,
        bool requireObjectMode) {
    bool changed = false;
    if (requireObjectMode && session.selection_mode() != quader_editor_core::SelectionMode::object) {
        session.set_selection_mode(quader_editor_core::SelectionMode::object);
        changed = true;
    }
    if ((!requireObjectMode || session.selection_mode() == quader_editor_core::SelectionMode::object) &&
            runtimeSelectionMatches(session, projection)) {
        return changed;
    }
    session.set_selected_object_ids(projection.objectIds, projection.activeObjectId);
    return true;
}

std::vector<EntityId> sceneEntitiesForRuntimeObjects(
        std::span<const int> objectIds,
        const ModelingObjectRuntimeMap& objectRegistry) {
    std::vector<EntityId> entities;
    entities.reserve(objectIds.size());
    for (const int objectId : objectIds) {
        const std::optional<EntityId> entity = objectRegistry.entityForModelingObject(objectId);
        if (entity.has_value()) {
            entities.push_back(*entity);
        }
    }
    return entities;
}

EntityId activeSceneEntityForRuntimeObject(
        int activeObjectId,
        const std::unordered_set<EntityId>& desiredSelection,
        const ModelingObjectRuntimeMap& objectRegistry) {
    const std::optional<EntityId> activeEntity = objectRegistry.entityForModelingObject(activeObjectId);
    if (activeEntity.has_value() && desiredSelection.contains(*activeEntity)) {
        return *activeEntity;
    }
    return InvalidEntityId;
}

bool applyExactSceneSelection(
        SceneDocument& scene,
        std::span<const EntityId> selected,
        EntityId active) {
    const std::uint64_t revisionBefore = scene.revision();
    std::unordered_set<EntityId> desired;
    desired.reserve(selected.size());
    std::vector<EntityId> orderedSelected;
    orderedSelected.reserve(selected.size());

    for (const EntityId entity : selected) {
        if (scene.exists(entity) && !desired.contains(entity)) {
            desired.insert(entity);
            orderedSelected.push_back(entity);
        }
    }

    for (const EntityId entity : scene.selectedEntities()) {
        if (!desired.contains(entity)) {
            scene.setSelected(entity, false);
        }
    }
    for (const EntityId entity : orderedSelected) {
        const SelectionComponent* selection = scene.selection(entity);
        if (selection == nullptr || !selection->selected) {
            scene.setSelected(entity, true);
        }
    }

    const EntityId resolvedActive = resolveProjectedActiveSelection<EntityId>(
            std::span<const EntityId>(orderedSelected.data(), orderedSelected.size()),
            active,
            InvalidEntityId);
    if (resolvedActive != InvalidEntityId) {
        scene.setActiveSelection(resolvedActive);
    }

    return scene.revision() != revisionBefore;
}

QVec3 worldPointForModelingObject(
        int objectId,
        QVec3 point,
        const ModelingObjectRuntimeMap& objectRegistry,
        const SceneDocument& scene) {
    const std::optional<EntityId> entity = objectRegistry.entityForModelingObject(objectId);
    if (!entity.has_value()) {
        return point;
    }
    const TransformComponent* transform = scene.transform(*entity);
    if (transform == nullptr) {
        return point;
    }
    return transformPoint(scene.worldTransformMatrix(*entity), point);
}

} // namespace

bool SelectionProjectionService::shouldProjectObjectSelectionFromScene(
        const quader_editor_core::EditorSession& session,
        bool forceObjectMode) const {
    return forceObjectMode || session.selection_mode() == quader_editor_core::SelectionMode::object;
}

bool SelectionProjectionService::projectObjectSelectionFromScene(
        quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        bool forceObjectMode) const {
    if (!shouldProjectObjectSelectionFromScene(session, forceObjectMode)) {
        return false;
    }

    const std::vector<EntityId> selectedEntities = scene.selectedEntities();
    const RuntimeObjectSelectionProjection projection = runtimeObjectSelectionForSceneEntities(
            selectedEntities,
            scene.primarySelection(),
            objectRegistry);
    return applyRuntimeObjectSelection(session, projection, forceObjectMode);
}

RuntimeObjectSelectionProjectionResult SelectionProjectionService::projectObjectSelectionForSceneEntities(
        quader_editor_core::EditorSession& session,
        std::span<const EntityId> entities,
        EntityId active,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    RuntimeObjectSelectionProjectionResult result;

    const RuntimeObjectSelectionProjection projection =
            runtimeObjectSelectionForSceneEntities(entities, active, objectRegistry);
    result.hasModelingObjects = projection.hasModelingObjects;
    if (projection.hasInputEntities && !projection.hasModelingObjects) {
        result.ok = false;
        return result;
    }

    result.changed = applyRuntimeObjectSelection(session, projection, false);
    return result;
}

bool SelectionProjectionService::promoteObjectSelectionForTool(
        quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    const std::vector<EntityId> selectedEntities = scene.selectedEntities();
    const RuntimeObjectSelectionProjection projection = runtimeObjectSelectionForSceneEntities(
            selectedEntities,
            scene.primarySelection(),
            objectRegistry);
    if (projection.hasModelingObjects) {
        return applyRuntimeObjectSelection(session, projection, true);
    }

    const std::vector<int> currentObjectIds = session.selected_object_ids();
    if (currentObjectIds.empty()) {
        return false;
    }
    const int activeObjectId = session.active_object_id() > 0 ? session.active_object_id() : currentObjectIds.back();
    if (session.selection_mode() != quader_editor_core::SelectionMode::object) {
        session.set_selection_mode(quader_editor_core::SelectionMode::object);
    }
    session.set_selected_object_ids(currentObjectIds, activeObjectId);
    return true;
}

bool SelectionProjectionService::projectSceneSelectionFromModeling(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    const std::vector<int> selectedObjectIds = session.selected_object_ids();
    const std::vector<EntityId> selectedEntities =
            sceneEntitiesForRuntimeObjects(selectedObjectIds, objectRegistry);
    const std::unordered_set<EntityId> desiredSelection(selectedEntities.begin(), selectedEntities.end());
    const EntityId active = activeSceneEntityForRuntimeObject(
            session.active_object_id(),
            desiredSelection,
            objectRegistry);
    return applyExactSceneSelection(scene, selectedEntities, active);
}

bool SelectionProjectionService::restoreSceneSelection(
        SceneDocument& scene,
        std::span<const EntityId> selected,
        EntityId active) const {
    return applyExactSceneSelection(scene, selected, active);
}

bool SelectionProjectionService::addSceneSelection(
        SceneDocument& scene,
        std::span<const EntityId> selected,
        EntityId active) const {
    const std::uint64_t revisionBefore = scene.revision();
    for (const EntityId entity : selected) {
        if (scene.exists(entity)) {
            scene.setSelected(entity, true);
        }
    }
    if (active != InvalidEntityId && scene.exists(active)) {
        scene.setActiveSelection(active);
    }
    return scene.revision() != revisionBefore;
}

std::optional<QVec3> SelectionProjectionService::worldSelectionCenter(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const AssetRegistry& assets,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    if (session.selection_mode() == quader_editor_core::SelectionMode::object) {
        if (const std::optional<SceneBounds> bounds = selectedWorldBounds(scene, assets)) {
            return boundsCenter(*bounds);
        }
    }

    std::optional<SceneBounds> bounds;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        if (!object.selected || object.selection.empty()) {
            continue;
        }
        const std::optional<EntityId> entity = objectRegistry.entityForModelingObject(object.id);
        if (entity.has_value() && !scene.isEffectivelyVisible(*entity)) {
            continue;
        }

        const std::vector<quader_poly::ElementId> vertexIds =
                quader_poly::selected_vertex_ids(object.document, object.selection);
        for (const quader_poly::ElementId vertexId : vertexIds) {
            const quader_poly::Vertex* vertex = quader_poly::find_vertex(object.document, vertexId);
            if (vertex == nullptr) {
                continue;
            }
            const QVec3 world = worldPointForModelingObject(
                    object.id,
                    vertex->position,
                    objectRegistry,
                    scene);
            if (!bounds) {
                bounds = SceneBounds{.min = world, .max = world};
            } else {
                includePoint(*bounds, world);
            }
        }
    }
    if (bounds) {
        return boundsCenter(*bounds);
    }

    const std::optional<quader::QVec3> center = session.selection_center();
    return center ? std::optional<QVec3>{*center} : std::nullopt;
}

} // namespace quader

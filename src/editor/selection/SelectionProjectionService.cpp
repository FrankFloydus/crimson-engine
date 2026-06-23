////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/SelectionProjectionService.h"

#include "editor/modeling/runtime/ModelingObjectRuntimeMap.h"
#include "scene/Components.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <editor/modeling/runtime/session.h>

namespace quader {
namespace {

bool applyExactSceneSelection(SceneDocument& scene, const std::unordered_set<EntityId>& desired, EntityId active) {
    const std::uint64_t revisionBefore = scene.revision();

    for (const EntityId entity : scene.selectedEntities()) {
        if (!desired.contains(entity)) {
            scene.setSelected(entity, false);
        }
    }
    for (const EntityId entity : desired) {
        const SelectionComponent* selection = scene.selection(entity);
        if (selection == nullptr || !selection->selected) {
            scene.setSelected(entity, true);
        }
    }
    if (active != InvalidEntityId && desired.contains(active)) {
        const SelectionComponent* selection = scene.selection(active);
        if (selection != nullptr && selection->selected) {
            scene.setActiveSelection(active);
        }
    }

    return scene.revision() != revisionBefore;
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

    std::vector<int> objectIds;
    objectIds.reserve(scene.selectedEntities().size());
    for (const EntityId entity : scene.selectedEntities()) {
        const auto object = objectRegistry.objectsByEntity().find(entity);
        if (object != objectRegistry.objectsByEntity().end()) {
            objectIds.push_back(object->second);
        }
    }

    int activeObjectId = -1;
    const auto primary = objectRegistry.objectsByEntity().find(scene.primarySelection());
    if (primary != objectRegistry.objectsByEntity().end()) {
        activeObjectId = primary->second;
    }

    std::vector<int> currentObjectIds = session.selected_object_ids();
    std::sort(currentObjectIds.begin(), currentObjectIds.end());
    std::vector<int> desiredObjectIds = objectIds;
    std::sort(desiredObjectIds.begin(), desiredObjectIds.end());
    const bool alreadyObjectMode = session.selection_mode() == quader_editor_core::SelectionMode::object;
    if ((!forceObjectMode || alreadyObjectMode) &&
            currentObjectIds == desiredObjectIds &&
            session.active_object_id() == activeObjectId) {
        return false;
    }

    session.set_selected_object_ids(objectIds, activeObjectId);
    return true;
}

bool SelectionProjectionService::projectSceneSelectionFromModeling(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    const std::vector<int> selectedObjectIds = session.selected_object_ids();
    const int activeObjectId = session.active_object_id();
    std::unordered_set<EntityId> desiredSelection;
    desiredSelection.reserve(selectedObjectIds.size());
    for (const int objectId : selectedObjectIds) {
        const auto entity = objectRegistry.entitiesByObject().find(objectId);
        if (entity != objectRegistry.entitiesByObject().end()) {
            desiredSelection.insert(entity->second);
        }
    }

    EntityId active = InvalidEntityId;
    const auto activeEntity = objectRegistry.entitiesByObject().find(activeObjectId);
    if (activeEntity != objectRegistry.entitiesByObject().end() && desiredSelection.contains(activeEntity->second)) {
        active = activeEntity->second;
    }
    return applyExactSceneSelection(scene, desiredSelection, active);
}

bool SelectionProjectionService::restoreSceneSelection(
        SceneDocument& scene,
        std::span<const EntityId> selected,
        EntityId active) const {
    const std::uint64_t revisionBefore = scene.revision();
    std::unordered_set<EntityId> desired;
    desired.reserve(selected.size());
    for (const EntityId entity : selected) {
        if (scene.exists(entity)) {
            desired.insert(entity);
        }
    }

    for (const EntityId entity : scene.selectedEntities()) {
        if (!desired.contains(entity)) {
            scene.setSelected(entity, false);
        }
    }
    for (const EntityId entity : selected) {
        if (scene.exists(entity)) {
            scene.setSelected(entity, true);
        }
    }

    if (active != InvalidEntityId && desired.contains(active)) {
        scene.setActiveSelection(active);
        return scene.revision() != revisionBefore;
    }
    for (const EntityId entity : selected) {
        if (scene.exists(entity)) {
            scene.setActiveSelection(entity);
            return scene.revision() != revisionBefore;
        }
    }
    return scene.revision() != revisionBefore;
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

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/ModelingHierarchyDuplicateService.h"

#include "editor/commands/DocumentCommandService.h"
#include "editor/commands/document/DocumentCommandSceneHelpers.h"
#include "editor/modeling/runtime/ModelingObjectRuntimeMap.h"
#include "scene/Components.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <editor/modeling/runtime/session.h>

namespace quader {
namespace {

std::vector<EntityId> sourceMeshEntitiesInTreeOrder(const SceneDocument& scene, const std::vector<EntityId>& roots) {
    std::vector<EntityId> sourceMeshes;
    for (const EntityId root : roots) {
        for (const EntityId entity : scene.descendants(root, true)) {
            if (scene.modelingObject(entity) != nullptr) {
                sourceMeshes.push_back(entity);
            }
        }
    }
    return sourceMeshes;
}

std::vector<EntityId> sourceMeshEntitiesInRuntimeOrder(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        const std::vector<EntityId>& treeOrder) {
    if (treeOrder.empty()) {
        return {};
    }

    const std::unordered_set<EntityId> sourceMeshSet(treeOrder.begin(), treeOrder.end());
    std::vector<EntityId> sourceMeshes;
    sourceMeshes.reserve(treeOrder.size());
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const std::optional<EntityId> entity = objectRegistry.entityForModelingObject(object.id);
        if (entity.has_value() && sourceMeshSet.contains(*entity)) {
            sourceMeshes.push_back(*entity);
        }
    }
    return sourceMeshes;
}

std::vector<EntityId> selectedDuplicateMeshEntities(
        const quader_editor_core::EditorSession& session,
        const ModelingObjectRuntimeMap& objectRegistry) {
    const std::vector<int> selectedDuplicateObjectIds = session.selected_object_ids();
    const std::unordered_set<int> duplicateObjectSet(
            selectedDuplicateObjectIds.begin(),
            selectedDuplicateObjectIds.end());
    std::vector<EntityId> duplicateMeshes;
    duplicateMeshes.reserve(selectedDuplicateObjectIds.size());
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        if (!duplicateObjectSet.contains(object.id)) {
            continue;
        }
        const std::optional<EntityId> entity = objectRegistry.entityForModelingObject(object.id);
        if (entity.has_value()) {
            duplicateMeshes.push_back(*entity);
        }
    }
    return duplicateMeshes;
}

void restoreGroupSelection(
        DocumentCommandService& documentCommands,
        const ModelingHierarchyDuplicateRuntime& runtime,
        const std::vector<EntityId>& roots) {
    (void)documentCommands.selectEntities(roots, roots.empty() ? InvalidEntityId : roots.front());
    if (runtime.setSelectionMode) {
        (void)runtime.setSelectionMode(ModelingSelectionMode::Group);
    }
}

std::unordered_map<EntityId, EntityId> pairDuplicateMeshes(
        const std::vector<EntityId>& sourceMeshes,
        const std::vector<EntityId>& duplicateMeshes) {
    std::unordered_map<EntityId, EntityId> cloneBySource;
    for (std::size_t index = 0; index < sourceMeshes.size() && index < duplicateMeshes.size(); ++index) {
        cloneBySource[sourceMeshes[index]] = duplicateMeshes[index];
    }
    return cloneBySource;
}

} // namespace

ModelingCommandResult ModelingHierarchyDuplicateService::duplicateSelectedHierarchyForGroupDrag(
        quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        ModelingObjectRuntimeMap& objectRegistry,
        DocumentCommandService& documentCommands,
        ModelingHierarchyDuplicateRuntime runtime) const {
    const std::vector<EntityId> roots = documentCommandSelectedRootEntities(scene);
    if (roots.empty()) {
        return {.ok = false, .changed = false, .message = "Select a group to duplicate."};
    }

    std::vector<EntityId> sourceMeshes = sourceMeshEntitiesInTreeOrder(scene, roots);
    if (!sourceMeshes.empty()) {
        if (runtime.ensureSceneObjectsSynced) {
            runtime.ensureSceneObjectsSynced();
        }
        sourceMeshes = sourceMeshEntitiesInRuntimeOrder(session, scene, objectRegistry, sourceMeshes);
    }

    std::vector<EntityId> duplicateMeshes;
    if (!sourceMeshes.empty()) {
        if (!runtime.setSelectionMode || !runtime.projectObjectSelectionFromScene || !runtime.duplicateSelectedMeshes) {
            restoreGroupSelection(documentCommands, runtime, roots);
            return {.ok = false, .changed = false, .message = "Modeling hierarchy duplicate runtime is unavailable."};
        }

        const ModelingCommandResult objectMode = runtime.setSelectionMode(ModelingSelectionMode::Object);
        if (!objectMode.ok) {
            restoreGroupSelection(documentCommands, runtime, roots);
            return objectMode;
        }
        (void)documentCommands.selectEntities(sourceMeshes, sourceMeshes.front());
        runtime.projectObjectSelectionFromScene();
        const ModelingCommandResult duplicate = runtime.duplicateSelectedMeshes();
        if (!duplicate.ok) {
            restoreGroupSelection(documentCommands, runtime, roots);
            return duplicate;
        }

        duplicateMeshes = selectedDuplicateMeshEntities(session, objectRegistry);
        if (duplicateMeshes.size() != sourceMeshes.size()) {
            restoreGroupSelection(documentCommands, runtime, roots);
            return {
                    .ok = false,
                    .changed = duplicate.changed,
                    .message = "Group duplicate did not create matching mesh clones.",
            };
        }
    }

    const std::unordered_map<EntityId, EntityId> cloneBySource = pairDuplicateMeshes(sourceMeshes, duplicateMeshes);
    const DocumentDuplicateHierarchyResult duplicated =
            documentCommands.duplicateHierarchyWithExistingClones(roots, cloneBySource);
    if (!duplicated.ok || duplicated.duplicateRoots.empty()) {
        restoreGroupSelection(documentCommands, runtime, roots);
        return {
                .ok = duplicated.ok,
                .changed = duplicated.changed,
                .message = duplicated.message.empty() ? "Group duplicate failed." : duplicated.message,
        };
    }

    (void)documentCommands.selectEntities(duplicated.duplicateRoots, duplicated.duplicateRoots.front());
    if (runtime.setSelectionMode) {
        (void)runtime.setSelectionMode(ModelingSelectionMode::Group);
    }
    return {.ok = true, .changed = true, .message = "Group duplicated."};
}

} // namespace quader

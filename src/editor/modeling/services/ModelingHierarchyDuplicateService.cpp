////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/ModelingHierarchyDuplicateService.h"

#include "editor/commands/DocumentCommandService.h"
#include "editor/modeling/runtime/ModelingObjectRuntimeMap.h"
#include "scene/Components.h"
#include "scene/SceneDocument.h"
#include "scene/SceneEntityNaming.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <editor/modeling/runtime/session.h>

namespace quader {
namespace {

std::vector<EntityId> selectedRootEntities(const SceneDocument& scene) {
    const std::vector<EntityId> selected = scene.selectedEntities();
    std::unordered_set<EntityId> selectedSet(selected.begin(), selected.end());
    std::vector<EntityId> roots;
    for (const EntityId entity : scene.entityTreeOrder()) {
        if (!selectedSet.contains(entity)) {
            continue;
        }
        bool ancestorSelected = false;
        EntityId parent = scene.parent(entity);
        while (parent != InvalidEntityId) {
            if (selectedSet.contains(parent)) {
                ancestorSelected = true;
                break;
            }
            parent = scene.parent(parent);
        }
        if (!ancestorSelected) {
            roots.push_back(entity);
        }
    }
    return roots;
}

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

void copyCommonEntityState(
        const SceneDocument& scene,
        DocumentCommandService& documentCommands,
        EntityId source,
        EntityId clone,
        bool renameClone = true) {
    if (renameClone) {
        if (const NameComponent* name = scene.name(source)) {
            (void)documentCommands.renameEntity(clone, SceneEntityNaming::nextName(scene, name->name));
        }
    }
    if (const TransformComponent* sourceTransform = scene.transform(source)) {
        (void)documentCommands.applyEntityTransform(clone, *sourceTransform);
    }
    if (const VisibilityComponent* visibility = scene.visibility(source)) {
        (void)documentCommands.setVisible(clone, visibility->visible);
    }
    if (const LockComponent* lock = scene.lock(source)) {
        (void)documentCommands.setLocked(clone, lock->locked);
    }
}

EntityId cloneHierarchyEntity(
        const SceneDocument& scene,
        DocumentCommandService& documentCommands,
        const std::unordered_map<EntityId, EntityId>& cloneBySource,
        EntityId source) {
    EntityId clone = InvalidEntityId;
    if (scene.modelingObject(source) != nullptr) {
        const auto mapped = cloneBySource.find(source);
        if (mapped == cloneBySource.end()) {
            return InvalidEntityId;
        }
        clone = mapped->second;
        copyCommonEntityState(scene, documentCommands, source, clone);
    } else {
        const NameComponent* name = scene.name(source);
        const std::string baseName = name ? name->name : "Entity";
        TransformComponent transform;
        if (const TransformComponent* sourceTransform = scene.transform(source)) {
            transform = *sourceTransform;
        }
        const DocumentCreateEntityResult created = documentCommands.createEntity(
                SceneEntityNaming::nextName(scene, baseName),
                scene.kind(source),
                InvalidEntityId,
                transform);
        if (!created.ok || !created.changed) {
            return InvalidEntityId;
        }
        clone = created.entity;
        copyCommonEntityState(scene, documentCommands, source, clone, false);
    }

    for (const EntityId child : scene.children(source)) {
        const EntityId childClone = cloneHierarchyEntity(scene, documentCommands, cloneBySource, child);
        if (childClone != InvalidEntityId) {
            (void)documentCommands.reparentEntity(childClone, clone, false);
        }
    }
    return clone;
}

} // namespace

ModelingCommandResult ModelingHierarchyDuplicateService::duplicateSelectedHierarchyForGroupDrag(
        quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        ModelingObjectRuntimeMap& objectRegistry,
        DocumentCommandService& documentCommands,
        ModelingHierarchyDuplicateRuntime runtime) const {
    const std::vector<EntityId> roots = selectedRootEntities(scene);
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
    std::vector<EntityId> rootClones;
    for (const EntityId root : roots) {
        const EntityId clone = cloneHierarchyEntity(scene, documentCommands, cloneBySource, root);
        if (clone != InvalidEntityId) {
            (void)documentCommands.reparentEntity(clone, scene.parent(root), false);
            rootClones.push_back(clone);
        }
    }

    if (rootClones.empty()) {
        restoreGroupSelection(documentCommands, runtime, roots);
        return {.ok = false, .changed = false, .message = "Group duplicate failed."};
    }

    (void)documentCommands.selectEntities(rootClones, rootClones.front());
    if (runtime.setSelectionMode) {
        (void)runtime.setSelectionMode(ModelingSelectionMode::Group);
    }
    return {.ok = true, .changed = true, .message = "Group duplicated."};
}

} // namespace quader

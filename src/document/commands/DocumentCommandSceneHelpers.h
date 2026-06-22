////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "document/commands/DocumentCommandResult.h"
#include "scene/Components.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

/**
 * Stores the Document Selection State data contract used by the editor document command layer.
 */
struct DocumentSelectionState {
    std::vector<EntityId> selected;
    EntityId active = InvalidEntityId;
};

/**
 * Stores the Document Entity Snapshot data contract used by the editor document command layer.
 */
struct DocumentEntitySnapshot {
    EntityId originalId = InvalidEntityId;
    EditorObjectId editorObjectId = InvalidEditorObjectId;
    EntityId originalParent = InvalidEntityId;
    std::string name;
    EntityKind kind = EntityKind::Generic;
    TransformComponent transform;
    VisibilityComponent visibility;
    LockComponent lock;
    SelectionComponent selection;
    std::optional<ModelingObjectComponent> modelingObject;
    std::optional<MeshComponent> mesh;
    std::optional<MaterialComponent> material;
    std::optional<LightComponent> light;
    std::vector<DocumentEntitySnapshot> children;
};

/**
 * Represents a Document Entity Placement value used by the editor document command layer.
 */
struct DocumentEntityPlacement {
    EntityId entity = InvalidEntityId;
    EntityId parent = InvalidEntityId;
    TransformComponent transform;
};

inline std::vector<EntityId> documentCommandNormalizeExistingEntities(
        const SceneDocument& scene,
        std::vector<EntityId> entities) {
    std::vector<EntityId> result;
    result.reserve(entities.size());
    for (const EntityId entity : entities) {
        if (entity != InvalidEntityId && scene.exists(entity)) {
            result.push_back(entity);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

inline DocumentSelectionState documentCommandCaptureSelection(const SceneDocument& scene) {
    return {
            .selected = scene.selectedEntities(),
            .active = scene.primarySelection(),
    };
}

inline DocumentSelectionState documentCommandSelectionFor(
        const SceneDocument& scene,
        const std::vector<EntityId>& entities,
        EntityId active = InvalidEntityId) {
    EntityId resolvedActive = active;
    if (resolvedActive == InvalidEntityId) {
        for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
            if (*it != InvalidEntityId && scene.exists(*it)) {
                resolvedActive = *it;
                break;
            }
        }
    }

    std::vector<EntityId> selected = documentCommandNormalizeExistingEntities(scene, entities);
    if (std::find(selected.begin(), selected.end(), resolvedActive) == selected.end()) {
        resolvedActive = selected.empty() ? InvalidEntityId : selected.front();
    }
    return {
            .selected = std::move(selected),
            .active = resolvedActive,
    };
}

inline bool documentCommandSameSelection(const DocumentSelectionState& left, const DocumentSelectionState& right) {
    return left.active == right.active && left.selected == right.selected;
}

inline void documentCommandApplySelection(SceneDocument& scene, const DocumentSelectionState& state) {
    scene.clearSelection();
    for (const EntityId entity : state.selected) {
        if (scene.exists(entity)) {
            scene.setSelected(entity, true);
        }
    }
    if (state.active != InvalidEntityId && scene.exists(state.active)) {
        scene.setActiveSelection(state.active);
    }
}

inline EntityId documentCommandRemapEntity(
        EntityId entity,
        const std::unordered_map<EntityId, EntityId>& remappedEntities) {
    const auto mapped = remappedEntities.find(entity);
    return mapped == remappedEntities.end() ? entity : mapped->second;
}

inline DocumentSelectionState documentCommandRemapSelection(
        const DocumentSelectionState& state,
        const std::unordered_map<EntityId, EntityId>& remappedEntities) {
    DocumentSelectionState result;
    result.selected.reserve(state.selected.size());
    for (const EntityId entity : state.selected) {
        result.selected.push_back(documentCommandRemapEntity(entity, remappedEntities));
    }
    std::sort(result.selected.begin(), result.selected.end());
    result.selected.erase(std::unique(result.selected.begin(), result.selected.end()), result.selected.end());
    result.active = documentCommandRemapEntity(state.active, remappedEntities);
    return result;
}

inline std::optional<DocumentEntitySnapshot> documentCommandCaptureEntitySnapshot(
        const SceneDocument& scene,
        EntityId entity) {
    if (entity == InvalidEntityId || !scene.exists(entity)) {
        return std::nullopt;
    }

    DocumentEntitySnapshot snapshot;
    snapshot.originalId = entity;
    snapshot.editorObjectId = scene.editorObjectId(entity);
    snapshot.originalParent = scene.parent(entity);
    snapshot.kind = scene.kind(entity);
    if (const NameComponent* component = scene.name(entity)) {
        snapshot.name = component->name;
    }
    if (const TransformComponent* component = scene.transform(entity)) {
        snapshot.transform = *component;
    }
    if (const VisibilityComponent* component = scene.visibility(entity)) {
        snapshot.visibility = *component;
    }
    if (const LockComponent* component = scene.lock(entity)) {
        snapshot.lock = *component;
    }
    if (const SelectionComponent* component = scene.selection(entity)) {
        snapshot.selection = *component;
    }
    if (const ModelingObjectComponent* component = scene.modelingObject(entity)) {
        snapshot.modelingObject = *component;
    }
    if (const MeshComponent* component = scene.mesh(entity)) {
        snapshot.mesh = *component;
    }
    if (const MaterialComponent* component = scene.material(entity)) {
        snapshot.material = *component;
    }
    if (const LightComponent* component = scene.light(entity)) {
        snapshot.light = *component;
    }

    const std::vector<EntityId> children = scene.children(entity);
    snapshot.children.reserve(children.size());
    for (const EntityId child : children) {
        if (std::optional<DocumentEntitySnapshot> childSnapshot =
                    documentCommandCaptureEntitySnapshot(scene, child)) {
            snapshot.children.push_back(std::move(*childSnapshot));
        }
    }
    return snapshot;
}

inline void documentCommandApplyEntitySnapshotComponents(
        SceneDocument& scene,
        EntityId entity,
        const DocumentEntitySnapshot& snapshot,
        bool includeModelingObject) {
    scene.setTransform(entity, snapshot.transform);
    scene.setVisible(entity, snapshot.visibility.visible);
    scene.setLocked(entity, snapshot.lock.locked);
    if (snapshot.mesh.has_value()) {
        scene.setMesh(entity, *snapshot.mesh);
    }
    if (snapshot.material.has_value()) {
        scene.setMaterial(entity, *snapshot.material);
    }
    if (snapshot.light.has_value()) {
        scene.setLight(entity, *snapshot.light);
    }
    if (includeModelingObject && snapshot.modelingObject.has_value()) {
        scene.setModelingObject(entity, *snapshot.modelingObject);
    }
    scene.setEntityKind(entity, snapshot.kind);
}

inline EntityId documentCommandCreateEntityFromSnapshot(
        SceneDocument& scene,
        const DocumentEntitySnapshot& snapshot,
        EntityId parent,
        std::string name,
        bool includeModelingObject,
        bool preserveIdentity = false) {
    EntityId entity = preserveIdentity
            ? scene.createEntityWithId(
                    snapshot.originalId,
                    name.empty() ? snapshot.name : std::move(name),
                    snapshot.kind,
                    snapshot.editorObjectId)
            : scene.createEntity(name.empty() ? snapshot.name : std::move(name), snapshot.kind);
    if (entity == InvalidEntityId) {
        return InvalidEntityId;
    }
    if (parent != InvalidEntityId && scene.exists(parent)) {
        scene.setParent(entity, parent, false);
    }
    documentCommandApplyEntitySnapshotComponents(scene, entity, snapshot, includeModelingObject);
    return entity;
}

inline EntityId documentCommandRestoreEntitySnapshotTree(
        SceneDocument& scene,
        const DocumentEntitySnapshot& snapshot,
        EntityId parent,
        std::unordered_map<EntityId, EntityId>& remappedEntities,
        bool includeModelingObject = true) {
    const EntityId restored =
            documentCommandCreateEntityFromSnapshot(
                    scene,
                    snapshot,
                    parent,
                    snapshot.name,
                    includeModelingObject,
                    true);
    if (restored == InvalidEntityId) {
        return InvalidEntityId;
    }
    remappedEntities[snapshot.originalId] = restored;
    for (const DocumentEntitySnapshot& child : snapshot.children) {
        if (documentCommandRestoreEntitySnapshotTree(scene, child, restored, remappedEntities, includeModelingObject) ==
                InvalidEntityId) {
            return InvalidEntityId;
        }
    }
    return restored;
}

inline std::vector<EntityId> documentCommandSelectedRootEntities(const SceneDocument& scene) {
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

inline EntityId documentCommandCommonParent(const SceneDocument& scene, const std::vector<EntityId>& entities) {
    if (entities.empty()) {
        return InvalidEntityId;
    }

    const EntityId parent = scene.parent(entities.front());
    for (const EntityId entity : entities) {
        if (scene.parent(entity) != parent) {
            return InvalidEntityId;
        }
    }
    return parent;
}

inline void documentCommandSelectOnly(SceneDocument& scene, const std::vector<EntityId>& entities) {
    documentCommandApplySelection(scene, documentCommandSelectionFor(scene, entities));
}

inline std::vector<DocumentEntityPlacement> documentCommandCapturePlacements(
        const SceneDocument& scene,
        const std::vector<EntityId>& entities) {
    std::vector<DocumentEntityPlacement> placements;
    placements.reserve(entities.size());
    for (const EntityId entity : entities) {
        if (!scene.exists(entity)) {
            continue;
        }
        const TransformComponent* transform = scene.transform(entity);
        placements.push_back(DocumentEntityPlacement{
                .entity = entity,
                .parent = scene.parent(entity),
                .transform = transform == nullptr ? TransformComponent{} : *transform,
        });
    }
    return placements;
}

inline DocumentCommandResult documentCommandApplyPlacement(SceneDocument& scene, const DocumentEntityPlacement& placement) {
    if (!scene.exists(placement.entity)) {
        return documentCommandMissingEntityResult();
    }
    if (placement.parent != InvalidEntityId && !scene.exists(placement.parent)) {
        return documentCommandInvalidRequestResult("Parent entity does not exist");
    }
    if (!scene.setParent(placement.entity, placement.parent, false)) {
        return documentCommandInvalidRequestResult("Entity cannot be reparented");
    }
    scene.setTransform(placement.entity, placement.transform);
    return documentCommandChangedResult();
}

} // namespace quader

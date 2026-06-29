////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/history/editor_history_service.hpp"
#include "scene/components.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

class SceneDocument;
class AssetRegistry;

/**
 * Coordinates Document Command Service responsibilities for the editor document ownership layer.
 */
class DocumentCommandService {
public:
    DocumentCommandService(SceneDocument& scene, EditorHistoryService& history, const AssetRegistry* assets = nullptr);

    DocumentCreateEntityResult createEntity(
            std::string name,
            EntityKind kind = EntityKind::Generic,
            EntityId parent = InvalidEntityId,
            TransformComponent transform = TransformComponent{});
    DocumentCommandResult destroyEntity(EntityId entity);
    DocumentCommandResult reparentEntity(
            EntityId entity,
            EntityId parent,
            bool preserveWorldTransform = true);
    DocumentGroupSelectionResult groupSelection(
            std::string groupName = {},
            TransformComponent groupTransform = TransformComponent{});
    DocumentCommandResult ungroupSelection();
    DocumentDuplicateEntityResult duplicateEntity(EntityId entity);
    DocumentDuplicateHierarchyResult duplicateSelectedHierarchy();
    DocumentDuplicateHierarchyResult duplicateHierarchyWithExistingClones(
            std::vector<EntityId> roots,
            std::unordered_map<EntityId, EntityId> existingClonesBySource);
    DocumentCreateEntityResult createPointLight(
            std::string name,
            LightComponent light,
            EntityId parent = InvalidEntityId,
            TransformComponent transform = TransformComponent{},
            bool selectCreated = false);
    DocumentCreateEntityResult createDirectionalLight(
            std::string name,
            LightComponent light,
            EntityId parent = InvalidEntityId,
            TransformComponent transform = TransformComponent{},
            bool selectCreated = false);
    DocumentCommandResult selectEntities(std::vector<EntityId> entities, EntityId active = InvalidEntityId);
    DocumentCommandResult applyEntityTransform(EntityId entity, TransformComponent transform);
    DocumentCommandResult renameEntity(EntityId entity, std::string name);
    DocumentCommandResult setVisible(EntityId entity, bool visible);
    DocumentCommandResult setVisibility(std::vector<DocumentVisibilityEdit> edits);
    DocumentCommandResult setLocked(EntityId entity, bool locked);
    DocumentCommandResult setLocks(std::vector<DocumentLockEdit> edits);
    DocumentCommandResult setMaterial(EntityId entity, AssetId material);
    DocumentCommandResult setMeshComponent(EntityId entity, MeshComponent mesh);
    DocumentCommandResult setLightComponent(
            EntityId entity,
            LightComponent light,
            UndoRedoExecuteOptions options = {});
    DocumentCommandResult setSceneSettings(
            EntityId entity,
            SceneSettingsComponent settings,
            UndoRedoExecuteOptions options = {});
    DocumentCommandResult setTransform(EntityId entity, TransformComponent transform);
    DocumentCommandResult setTransforms(std::vector<DocumentTransformEdit> edits);
    DocumentCommandResult applySceneBatch(std::string label, std::vector<DocumentSceneBatchOperation> operations);

private:
    SceneDocument& scene_;
    EditorHistoryService& history_;
    const AssetRegistry* assets_ = nullptr;
};

} // namespace quader

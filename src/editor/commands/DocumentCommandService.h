////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "editor/commands/document/DocumentCommandEdits.h"
#include "editor/commands/document/DocumentCommandResult.h"
#include "editor/history/EditorHistoryService.h"
#include "scene/Components.h"

#include <string>
#include <vector>

namespace quader {

class SceneDocument;

/**
 * Coordinates Document Command Service responsibilities for the editor document ownership layer.
 */
class DocumentCommandService {
public:
    DocumentCommandService(SceneDocument& scene, EditorHistoryService& history);

    DocumentCommandResult createEntity(
            std::string name,
            EntityKind kind = EntityKind::Generic,
            EntityId parent = InvalidEntityId,
            TransformComponent transform = TransformComponent{},
            EntityId* createdEntity = nullptr);
    DocumentCommandResult destroyEntity(EntityId entity);
    DocumentCommandResult reparentEntity(
            EntityId entity,
            EntityId parent,
            bool preserveWorldTransform = true);
    DocumentCommandResult groupSelection(
            std::string groupName = {},
            TransformComponent groupTransform = TransformComponent{},
            EntityId* groupEntity = nullptr);
    DocumentCommandResult ungroupSelection();
    DocumentCommandResult duplicateEntity(EntityId entity, EntityId* duplicateEntity = nullptr);
    DocumentCommandResult selectEntities(std::vector<EntityId> entities, EntityId active = InvalidEntityId);
    DocumentCommandResult applyEntityTransform(EntityId entity, TransformComponent transform);
    DocumentCommandResult renameEntity(EntityId entity, std::string name);
    DocumentCommandResult setVisible(EntityId entity, bool visible);
    DocumentCommandResult setVisibility(std::vector<DocumentVisibilityEdit> edits);
    DocumentCommandResult setLocked(EntityId entity, bool locked);
    DocumentCommandResult setLocks(std::vector<DocumentLockEdit> edits);
    DocumentCommandResult setMaterial(EntityId entity, AssetId material);
    DocumentCommandResult setMeshComponent(EntityId entity, MeshComponent mesh);
    DocumentCommandResult setTransform(EntityId entity, TransformComponent transform);
    DocumentCommandResult setTransforms(std::vector<DocumentTransformEdit> edits);

private:
    SceneDocument& scene_;
    EditorHistoryService& history_;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderSceneApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "editor/commands/document/ApplyEntityTransformCommand.h"
#include "editor/commands/document/BatchDocumentCommand.h"
#include "editor/commands/document/RenameEntityCommand.h"
#include "editor/commands/document/ReparentEntityCommand.h"
#include "editor/commands/document/SetLockCommand.h"
#include "editor/commands/document/SetVisibilityCommand.h"
#include "scene/document/ProjectSession.h"
#include "editor/selection/EditorSelectionService.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"

#include <memory>
#include <utility>
#include <vector>

namespace quader {
namespace {

std::string batchLabelOrDefault(std::string label) {
    return label.empty() ? "Scene Batch" : std::move(label);
}

bool isDefaultTransform(const TransformComponent& transform) {
    return transform.position.x == 0.0f &&
            transform.position.y == 0.0f &&
            transform.position.z == 0.0f &&
            transform.rotationEuler.x == 0.0f &&
            transform.rotationEuler.y == 0.0f &&
            transform.rotationEuler.z == 0.0f &&
            transform.scale.x == 1.0f &&
            transform.scale.y == 1.0f &&
            transform.scale.z == 1.0f;
}

TransformComponent groupTransformOrSelectedBoundsCenter(ProjectSession& session, TransformComponent transform) {
    if (!isDefaultTransform(transform)) {
        return transform;
    }

    if (const std::optional<SceneBounds> bounds = selectedWorldBounds(session.scene(), session.assets())) {
        transform.position = boundsCenter(*bounds);
    }
    return transform;
}

} // namespace

QuaderSceneBatch::QuaderSceneBatch(QuaderSceneApi& scene, std::string label)
    : scene_(&scene)
    , label_(batchLabelOrDefault(std::move(label))) {}

QuaderSceneBatch& QuaderSceneBatch::rename(EntityId entity, std::string name) {
    operations_.push_back(Operation{
            .kind = OperationKind::Rename,
            .entity = entity,
            .name = std::move(name),
    });
    return *this;
}

QuaderSceneBatch& QuaderSceneBatch::reparent(
        EntityId entity,
        EntityId parent,
        bool preserveWorldTransform) {
    operations_.push_back(Operation{
            .kind = OperationKind::Reparent,
            .entity = entity,
            .parent = parent,
            .preserveWorldTransform = preserveWorldTransform,
    });
    return *this;
}

QuaderSceneBatch& QuaderSceneBatch::setTransform(EntityId entity, TransformComponent transform) {
    operations_.push_back(Operation{
            .kind = OperationKind::SetTransform,
            .entity = entity,
            .transform = transform,
    });
    return *this;
}

QuaderSceneBatch& QuaderSceneBatch::setVisible(EntityId entity, bool visible) {
    operations_.push_back(Operation{
            .kind = OperationKind::SetVisible,
            .entity = entity,
            .flag = visible,
    });
    return *this;
}

QuaderSceneBatch& QuaderSceneBatch::setLocked(EntityId entity, bool locked) {
    operations_.push_back(Operation{
            .kind = OperationKind::SetLocked,
            .entity = entity,
            .flag = locked,
    });
    return *this;
}

QuaderApiVoidResult QuaderSceneBatch::commit() {
    if (scene_ == nullptr) {
        return {
                .ok = false,
                .changed = false,
                .message = "Scene batch is not attached.",
                .mutationKind = QuaderMutationKind::None,
        };
    }
    if (committed_) {
        return {
                .ok = false,
                .changed = false,
                .message = "Scene batch was already committed.",
                .mutationKind = QuaderMutationKind::None,
        };
    }
    committed_ = true;
    return scene_->commitBatch(*this);
}

QuaderSceneApi::QuaderSceneApi(ProjectSession& session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::createEntity(const QuaderCreateEntityArgs& args) {
    return mutations_.createEntity(args);
}

QuaderApiVoidResult QuaderSceneApi::destroy(EntityId entity) {
    return mutations_.runDocumentResult(session_.documentCommands().destroyEntity(entity), "Destroy Entity");
}

QuaderApiVoidResult QuaderSceneApi::rename(EntityId entity, std::string name) {
    return mutations_.runDocumentResult(
            session_.documentCommands().renameEntity(entity, std::move(name)),
            "Rename Entity");
}

QuaderApiVoidResult QuaderSceneApi::reparent(
        EntityId entity,
        EntityId parent,
        bool preserveWorldTransform) {
    return mutations_.runDocumentResult(
            session_.documentCommands().reparentEntity(entity, parent, preserveWorldTransform),
            "Reparent Entity");
}

QuaderApiVoidResult QuaderSceneApi::setTransform(EntityId entity, TransformComponent transform) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setTransform(entity, transform),
            "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::setTransforms(std::span<const DocumentTransformEdit> edits) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setTransforms(std::vector<DocumentTransformEdit>(edits.begin(), edits.end())),
            "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::setVisible(EntityId entity, bool visible) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setVisible(entity, visible),
            "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::setVisibility(std::span<const DocumentVisibilityEdit> edits) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setVisibility(std::vector<DocumentVisibilityEdit>(edits.begin(), edits.end())),
            "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::setLocked(EntityId entity, bool locked) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setLocked(entity, locked),
            "Set Lock");
}

QuaderApiVoidResult QuaderSceneApi::setLocks(std::span<const DocumentLockEdit> edits) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setLocks(std::vector<DocumentLockEdit>(edits.begin(), edits.end())),
            "Set Lock");
}

QuaderApiVoidResult QuaderSceneApi::setMeshComponent(EntityId entity, MeshComponent mesh) {
    return mutations_.runDocumentResult(
            session_.documentCommands().setMeshComponent(entity, std::move(mesh)),
            "Set Mesh Component");
}

QuaderApiVoidResult QuaderSceneApi::select(std::span<const EntityId> entities, EntityId active) {
    return mutations_.runDocumentResult(
            session_.selection().setSceneSelection(entities, active),
            "Select Entities");
}

QuaderApiVoidResult QuaderSceneApi::select(std::initializer_list<EntityId> entities, EntityId active) {
    return select(std::span<const EntityId>(entities.begin(), entities.size()), active);
}

QuaderApiVoidResult QuaderSceneApi::select(EntityId entity) {
    return select({entity}, entity);
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::duplicate(EntityId entity) {
    EntityId duplicateEntity = InvalidEntityId;
    DocumentCommandResult command = session_.documentCommands().duplicateEntity(entity, &duplicateEntity);

    QuaderApiResult<QuaderEntityRef> result;
    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Duplicate Entity");
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{
                .id = duplicateEntity,
                .editorObjectId = session_.scene().editorObjectId(duplicateEntity),
        };
    }
    return result;
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::groupSelection(std::string groupName, TransformComponent transform) {
    EntityId groupEntity = InvalidEntityId;
    DocumentCommandResult command = session_.documentCommands().groupSelection(
            std::move(groupName),
            groupTransformOrSelectedBoundsCenter(session_, transform),
            &groupEntity);

    QuaderApiResult<QuaderEntityRef> result;
    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Group Selection");
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{
                .id = groupEntity,
                .editorObjectId = session_.scene().editorObjectId(groupEntity),
        };
    }
    return result;
}

QuaderApiVoidResult QuaderSceneApi::ungroupSelection() {
    return mutations_.runDocumentResult(session_.documentCommands().ungroupSelection(), "Ungroup Selection");
}

QuaderSceneBatch QuaderSceneApi::batch(std::string label) {
    return QuaderSceneBatch(*this, std::move(label));
}

QuaderApiVoidResult QuaderSceneApi::commitBatch(const QuaderSceneBatch& batch) {
    SceneDocument& scene = session_.scene();
    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(batch.operations_.size());

    auto failure = [&](std::string message) {
        return mutations_.runDocumentCommand(commandInvalidResult(std::move(message)), batch.label_);
    };

    for (const QuaderSceneBatch::Operation& operation : batch.operations_) {
        if (operation.entity == InvalidEntityId || !scene.exists(operation.entity)) {
            return failure("Batch entity does not exist.");
        }

        switch (operation.kind) {
        case QuaderSceneBatch::OperationKind::Rename: {
            const NameComponent* current = scene.name(operation.entity);
            if (current == nullptr) {
                return failure("Batch entity cannot be renamed.");
            }
            if (current->name == operation.name) {
                break;
            }
            commands.push_back(std::make_unique<RenameEntityCommand>(
                    scene,
                    operation.entity,
                    current->name,
                    operation.name));
            break;
        }
        case QuaderSceneBatch::OperationKind::Reparent:
            if (operation.parent != InvalidEntityId && !scene.exists(operation.parent)) {
                return failure("Batch parent entity does not exist.");
            }
            if (scene.parent(operation.entity) == operation.parent) {
                break;
            }
            commands.push_back(std::make_unique<ReparentEntityCommand>(
                    scene,
                    operation.entity,
                    operation.parent,
                    operation.preserveWorldTransform));
            break;
        case QuaderSceneBatch::OperationKind::SetTransform: {
            const TransformComponent* current = scene.transform(operation.entity);
            if (current == nullptr) {
                return failure("Batch entity has no transform.");
            }
            if (scene.isEffectivelyLocked(operation.entity) ||
                    documentCommandSameTransform(*current, operation.transform)) {
                break;
            }
            commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
                    scene,
                    operation.entity,
                    *current,
                    operation.transform,
                    batch.label_));
            break;
        }
        case QuaderSceneBatch::OperationKind::SetVisible: {
            const VisibilityComponent* current = scene.visibility(operation.entity);
            if (current == nullptr) {
                return failure("Batch entity has no visibility component.");
            }
            if (current->visible == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetVisibilityCommand>(
                    scene,
                    operation.entity,
                    current->visible,
                    operation.flag));
            break;
        }
        case QuaderSceneBatch::OperationKind::SetLocked: {
            const LockComponent* current = scene.lock(operation.entity);
            if (current == nullptr) {
                return failure("Batch entity has no lock component.");
            }
            if (current->locked == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetLockCommand>(
                    scene,
                    operation.entity,
                    current->locked,
                    operation.flag));
            break;
        }
        }
    }

    if (commands.empty()) {
        return mutations_.runDocumentCommand(commandUnchangedResult(), batch.label_);
    }

    return mutations_.runDocumentCommand(
            session_.undoRedo().execute(std::make_unique<BatchDocumentCommand>(batch.label_, std::move(commands))),
            batch.label_);
}

} // namespace quader

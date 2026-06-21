#include "document/DocumentCommandService.h"

#include "document/commands/ApplyEntityTransformCommand.h"
#include "document/commands/BatchDocumentCommand.h"
#include "document/commands/CreateEntityCommand.h"
#include "document/commands/DestroyEntityCommand.h"
#include "document/commands/DuplicateEntityCommand.h"
#include "document/commands/GroupSelectionCommand.h"
#include "document/commands/RenameEntityCommand.h"
#include "document/commands/ReparentEntityCommand.h"
#include "document/commands/SelectEntitiesCommand.h"
#include "document/commands/SetLockCommand.h"
#include "document/commands/SetVisibilityCommand.h"
#include "document/commands/UngroupSelectionCommand.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace quader {
namespace {

DocumentCommandResult executeDocumentCommand(
        UndoRedoManager& undoRedo,
        std::unique_ptr<IUndoableCommand> command) {
    return undoRedo.execute(std::move(command));
}

std::vector<DocumentTransformEdit> normalizedTransformEdits(
        const SceneDocument& scene,
        std::vector<DocumentTransformEdit> edits) {
    std::vector<DocumentTransformEdit> result;
    result.reserve(edits.size());
    for (const DocumentTransformEdit& edit : edits) {
        if (edit.entity == InvalidEntityId ||
                !scene.exists(edit.entity) ||
                scene.isEffectivelyLocked(edit.entity) ||
                documentCommandSameTransform(edit.before, edit.after)) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

std::vector<DocumentVisibilityEdit> normalizedVisibilityEdits(
        const SceneDocument& scene,
        std::vector<DocumentVisibilityEdit> edits) {
    std::vector<DocumentVisibilityEdit> result;
    result.reserve(edits.size());
    for (const DocumentVisibilityEdit& edit : edits) {
        if (edit.entity == InvalidEntityId || !scene.exists(edit.entity) || edit.before == edit.after) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

std::vector<DocumentLockEdit> normalizedLockEdits(
        const SceneDocument& scene,
        std::vector<DocumentLockEdit> edits) {
    std::vector<DocumentLockEdit> result;
    result.reserve(edits.size());
    for (const DocumentLockEdit& edit : edits) {
        if (edit.entity == InvalidEntityId || !scene.exists(edit.entity) || edit.before == edit.after) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

} // namespace

DocumentCommandService::DocumentCommandService(SceneDocument& scene, UndoRedoManager& undoRedo)
    : scene_(scene)
    , undoRedo_(undoRedo) {}

DocumentCommandResult DocumentCommandService::createEntity(
        std::string name,
        EntityKind kind,
        EntityId parent,
        TransformComponent transform,
        EntityId* createdEntity) {
    if (createdEntity != nullptr) {
        *createdEntity = InvalidEntityId;
    }

    auto command = std::make_unique<CreateEntityCommand>(
            scene_,
            std::move(name),
            kind,
            parent,
            transform);
    CreateEntityCommand* createdCommand = command.get();
    DocumentCommandResult result = executeDocumentCommand(undoRedo_, std::move(command));
    if (result.ok && result.changed && createdEntity != nullptr) {
        *createdEntity = createdCommand->entity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::destroyEntity(EntityId entity) {
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }
    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<DestroyEntityCommand>(scene_, entity));
}

DocumentCommandResult DocumentCommandService::reparentEntity(
        EntityId entity,
        EntityId parent,
        bool preserveWorldTransform) {
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }
    if (parent != InvalidEntityId && !scene_.exists(parent)) {
        return documentCommandInvalidRequestResult("Parent entity does not exist");
    }
    if (scene_.parent(entity) == parent) {
        return documentCommandUnchangedResult();
    }
    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<ReparentEntityCommand>(scene_, entity, parent, preserveWorldTransform));
}

DocumentCommandResult DocumentCommandService::groupSelection(
        std::string groupName,
        TransformComponent groupTransform,
        EntityId* groupEntity) {
    if (groupEntity != nullptr) {
        *groupEntity = InvalidEntityId;
    }

    auto command = std::make_unique<GroupSelectionCommand>(
            scene_,
            std::move(groupName),
            groupTransform);
    GroupSelectionCommand* groupCommand = command.get();
    DocumentCommandResult result = executeDocumentCommand(undoRedo_, std::move(command));
    if (result.ok && result.changed && groupEntity != nullptr) {
        *groupEntity = groupCommand->groupEntity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::ungroupSelection() {
    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<UngroupSelectionCommand>(scene_));
}

DocumentCommandResult DocumentCommandService::duplicateEntity(EntityId entity, EntityId* duplicateEntity) {
    if (duplicateEntity != nullptr) {
        *duplicateEntity = InvalidEntityId;
    }
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }

    auto command = std::make_unique<DuplicateEntityCommand>(scene_, entity);
    DuplicateEntityCommand* duplicateCommand = command.get();
    DocumentCommandResult result = executeDocumentCommand(undoRedo_, std::move(command));
    if (result.ok && result.changed && duplicateEntity != nullptr) {
        *duplicateEntity = duplicateCommand->duplicateEntity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::selectEntities(std::vector<EntityId> entities, EntityId active) {
    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<SelectEntitiesCommand>(scene_, std::move(entities), active));
}

DocumentCommandResult DocumentCommandService::applyEntityTransform(EntityId entity, TransformComponent transform) {
    const TransformComponent* currentTransform = scene_.transform(entity);
    if (!currentTransform) {
        return documentCommandMissingEntityResult();
    }
    if (documentCommandSameTransform(*currentTransform, transform)) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<ApplyEntityTransformCommand>(scene_, entity, *currentTransform, transform));
}

DocumentCommandResult DocumentCommandService::renameEntity(EntityId entity, std::string name) {
    const NameComponent* currentName = scene_.name(entity);
    if (!currentName) {
        return documentCommandMissingEntityResult();
    }
    if (currentName->name == name) {
        return documentCommandUnchangedResult();
    }

    const std::string before = currentName->name;
    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<RenameEntityCommand>(scene_, entity, before, std::move(name)));
}

DocumentCommandResult DocumentCommandService::setVisible(EntityId entity, bool visible) {
    const VisibilityComponent* currentVisibility = scene_.visibility(entity);
    if (!currentVisibility) {
        return documentCommandMissingEntityResult();
    }
    if (currentVisibility->visible == visible) {
        return documentCommandUnchangedResult();
    }

    return setVisibility({DocumentVisibilityEdit{
            .entity = entity,
            .before = currentVisibility->visible,
            .after = visible,
    }});
}

DocumentCommandResult DocumentCommandService::setVisibility(std::vector<DocumentVisibilityEdit> edits) {
    edits = normalizedVisibilityEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return documentCommandUnchangedResult();
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(edits.size());
    for (const DocumentVisibilityEdit& edit : edits) {
        commands.push_back(std::make_unique<SetVisibilityCommand>(scene_, edit.entity, edit.before, edit.after));
    }

    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<BatchDocumentCommand>("Set Visibility", std::move(commands)));
}

DocumentCommandResult DocumentCommandService::setLocked(EntityId entity, bool locked) {
    const LockComponent* currentLock = scene_.lock(entity);
    if (!currentLock) {
        return documentCommandMissingEntityResult();
    }
    if (currentLock->locked == locked) {
        return documentCommandUnchangedResult();
    }

    return setLocks({DocumentLockEdit{
            .entity = entity,
            .before = currentLock->locked,
            .after = locked,
    }});
}

DocumentCommandResult DocumentCommandService::setLocks(std::vector<DocumentLockEdit> edits) {
    edits = normalizedLockEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return documentCommandUnchangedResult();
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(edits.size());
    for (const DocumentLockEdit& edit : edits) {
        commands.push_back(std::make_unique<SetLockCommand>(scene_, edit.entity, edit.before, edit.after));
    }

    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<BatchDocumentCommand>("Set Lock", std::move(commands)));
}

DocumentCommandResult DocumentCommandService::setTransform(EntityId entity, TransformComponent transform) {
    const TransformComponent* currentTransform = scene_.transform(entity);
    if (!currentTransform) {
        return documentCommandMissingEntityResult();
    }
    if (documentCommandSameTransform(*currentTransform, transform)) {
        return documentCommandUnchangedResult();
    }

    return setTransforms({DocumentTransformEdit{
            .entity = entity,
            .before = *currentTransform,
            .after = transform,
    }});
}

DocumentCommandResult DocumentCommandService::setTransforms(std::vector<DocumentTransformEdit> edits) {
    edits = normalizedTransformEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return documentCommandUnchangedResult();
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(edits.size());
    for (const DocumentTransformEdit& edit : edits) {
        commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
                scene_,
                edit.entity,
                edit.before,
                edit.after,
                "Set Transform"));
    }

    return executeDocumentCommand(
            undoRedo_,
            std::make_unique<BatchDocumentCommand>("Set Transform", std::move(commands)));
}

} // namespace quader

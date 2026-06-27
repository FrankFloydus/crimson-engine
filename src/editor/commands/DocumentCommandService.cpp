////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/DocumentCommandService.h"

#include "assets/AssetRegistry.h"
#include "editor/commands/document/ApplyEntityTransformCommand.h"
#include "editor/commands/document/BatchDocumentCommand.h"
#include "editor/commands/document/CreateEntityCommand.h"
#include "editor/commands/document/DestroyEntityCommand.h"
#include "editor/commands/document/DuplicateEntityCommand.h"
#include "editor/commands/document/DuplicateSelectedHierarchyCommand.h"
#include "editor/commands/document/GroupSelectionCommand.h"
#include "editor/commands/document/RenameEntityCommand.h"
#include "editor/commands/document/ReparentEntityCommand.h"
#include "editor/commands/document/SelectEntitiesCommand.h"
#include "editor/commands/document/SetLockCommand.h"
#include "editor/commands/document/SetMaterialCommand.h"
#include "editor/commands/document/SetMeshComponentCommand.h"
#include "editor/commands/document/SetVisibilityCommand.h"
#include "editor/commands/document/UngroupSelectionCommand.h"
#include "scene/SceneDocument.h"
#include "scene/SceneTransform.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

DocumentCommandResult executeDocumentCommand(
        EditorHistoryService& history,
        std::unique_ptr<IUndoableCommand> command) {
    return history.execute(std::move(command));
}

void assignDocumentCommandStatus(DocumentCommandResult& target, DocumentCommandResult source) {
    target.ok = source.ok;
    target.changed = source.changed;
    target.message = std::move(source.message);
}

bool documentCommandEntityIsModelingOwned(const SceneDocument& scene, EntityId entity) {
    return scene.modelingObject(entity) != nullptr;
}

DocumentCommandResult documentCommandModelingOwnedEntityResult() {
    return documentCommandInvalidRequestResult("Entity is owned by a modeling object; use modeling commands");
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

/**
 * Stores the simulated batch entity state used while converting scene batch operations to commands.
 */
struct DocumentSceneBatchEntityState {
    std::string name;
    EntityId parent = InvalidEntityId;
    TransformComponent transform;
    bool visible = true;
    bool locked = false;
};

} // namespace

DocumentCommandService::DocumentCommandService(
        SceneDocument& scene,
        EditorHistoryService& history,
        const AssetRegistry* assets)
    : scene_(scene)
    , history_(history)
    , assets_(assets) {}

DocumentCreateEntityResult DocumentCommandService::createEntity(
        std::string name,
        EntityKind kind,
        EntityId parent,
        TransformComponent transform) {
    auto command = std::make_unique<CreateEntityCommand>(
            scene_,
            std::move(name),
            kind,
            parent,
            transform);
    CreateEntityCommand* createdCommand = command.get();
    DocumentCreateEntityResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.entity = createdCommand->entity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::destroyEntity(EntityId entity) {
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }
    if (documentCommandEntityIsModelingOwned(scene_, entity)) {
        return documentCommandModelingOwnedEntityResult();
    }
    return executeDocumentCommand(
            history_,
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
            history_,
            std::make_unique<ReparentEntityCommand>(scene_, entity, parent, preserveWorldTransform));
}

DocumentGroupSelectionResult DocumentCommandService::groupSelection(
        std::string groupName,
        TransformComponent groupTransform) {
    auto command = std::make_unique<GroupSelectionCommand>(
            scene_,
            std::move(groupName),
            groupTransform);
    GroupSelectionCommand* groupCommand = command.get();
    DocumentGroupSelectionResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.groupEntity = groupCommand->groupEntity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::ungroupSelection() {
    return executeDocumentCommand(
            history_,
            std::make_unique<UngroupSelectionCommand>(scene_));
}

DocumentDuplicateEntityResult DocumentCommandService::duplicateEntity(EntityId entity) {
    if (!scene_.exists(entity)) {
        DocumentDuplicateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandMissingEntityResult());
        return result;
    }
    if (documentCommandEntityIsModelingOwned(scene_, entity)) {
        DocumentDuplicateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandModelingOwnedEntityResult());
        return result;
    }

    auto command = std::make_unique<DuplicateEntityCommand>(scene_, entity);
    DuplicateEntityCommand* duplicateCommand = command.get();
    DocumentDuplicateEntityResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.duplicateEntity = duplicateCommand->duplicateEntity();
    }
    return result;
}

DocumentDuplicateHierarchyResult DocumentCommandService::duplicateSelectedHierarchy() {
    auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(scene_);
    DuplicateSelectedHierarchyCommand* duplicateCommand = command.get();
    DocumentDuplicateHierarchyResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.duplicateRoots = duplicateCommand->duplicateRoots();
    }
    return result;
}

DocumentDuplicateHierarchyResult DocumentCommandService::duplicateHierarchyWithExistingClones(
        std::vector<EntityId> roots,
        std::unordered_map<EntityId, EntityId> existingClonesBySource) {
    std::unordered_set<EntityId> seenRoots;
    std::vector<EntityId> existingRoots;
    existingRoots.reserve(roots.size());
    for (const EntityId root : roots) {
        if (root != InvalidEntityId && scene_.exists(root) && seenRoots.insert(root).second) {
            existingRoots.push_back(root);
        }
    }
    roots = std::move(existingRoots);
    if (roots.empty()) {
        DocumentDuplicateHierarchyResult result;
        assignDocumentCommandStatus(result, documentCommandUnchangedResult());
        return result;
    }

    std::vector<DocumentEntitySnapshot> sourceSnapshots;
    sourceSnapshots.reserve(roots.size());
    for (const EntityId root : roots) {
        std::optional<DocumentEntitySnapshot> snapshot = documentCommandCaptureEntitySnapshot(scene_, root);
        if (!snapshot.has_value()) {
            DocumentDuplicateHierarchyResult result;
            assignDocumentCommandStatus(result, documentCommandMissingEntityResult());
            return result;
        }
        sourceSnapshots.push_back(std::move(*snapshot));
    }

    auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(
            scene_,
            std::move(sourceSnapshots),
            std::move(existingClonesBySource));
    DuplicateSelectedHierarchyCommand* duplicateCommand = command.get();
    DocumentDuplicateHierarchyResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.duplicateRoots = duplicateCommand->duplicateRoots();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::selectEntities(std::vector<EntityId> entities, EntityId active) {
    return executeDocumentCommand(
            history_,
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
            history_,
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
            history_,
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
            history_,
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
            history_,
            std::make_unique<BatchDocumentCommand>("Set Lock", std::move(commands)));
}

DocumentCommandResult DocumentCommandService::setMaterial(EntityId entity, AssetId material) {
    if (material == InvalidAssetId) {
        return documentCommandInvalidRequestResult("Material asset is invalid");
    }
    if (assets_ != nullptr && assets_->material(material) == nullptr) {
        return documentCommandInvalidRequestResult("Material asset does not exist");
    }
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }

    const MaterialComponent after{.material = material};
    std::optional<MaterialComponent> before;
    if (const MaterialComponent* currentMaterial = scene_.material(entity)) {
        if (*currentMaterial == after) {
            return documentCommandUnchangedResult();
        }
        before = *currentMaterial;
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<SetMaterialCommand>(
                    scene_,
                    entity,
                    before,
                    after));
}

DocumentCommandResult DocumentCommandService::setMeshComponent(EntityId entity, MeshComponent mesh) {
    if (mesh.mesh == InvalidAssetId) {
        return documentCommandInvalidRequestResult("Mesh asset is invalid");
    }
    if (assets_ != nullptr && assets_->mesh(mesh.mesh) == nullptr) {
        return documentCommandInvalidRequestResult("Mesh asset does not exist");
    }
    const MeshComponent* currentMesh = scene_.mesh(entity);
    if (!currentMesh) {
        return documentCommandMissingEntityResult();
    }
    if (documentCommandEntityIsModelingOwned(scene_, entity)) {
        return documentCommandModelingOwnedEntityResult();
    }
    if (documentCommandSameMeshComponent(*currentMesh, mesh)) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<SetMeshComponentCommand>(scene_, entity, *currentMesh, mesh));
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
            history_,
            std::make_unique<BatchDocumentCommand>("Set Transform", std::move(commands)));
}

DocumentCommandResult DocumentCommandService::applySceneBatch(
        std::string label,
        std::vector<DocumentSceneBatchOperation> operations) {
    if (label.empty()) {
        label = "Scene Batch";
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(operations.size());
    std::unordered_map<EntityId, DocumentSceneBatchEntityState> simulated;

    auto failure = [](std::string message) {
        return documentCommandInvalidRequestResult(std::move(message));
    };

    auto simulatedState = [&](EntityId entity)
            -> std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> {
        if (const auto existing = simulated.find(entity); existing != simulated.end()) {
            return existing->second;
        }
        const NameComponent* name = scene_.name(entity);
        const TransformComponent* transform = scene_.transform(entity);
        const VisibilityComponent* visibility = scene_.visibility(entity);
        const LockComponent* lock = scene_.lock(entity);
        if (name == nullptr || transform == nullptr || visibility == nullptr || lock == nullptr) {
            return std::nullopt;
        }
        auto [inserted, _] = simulated.emplace(entity, DocumentSceneBatchEntityState{
                .name = name->name,
                .parent = scene_.parent(entity),
                .transform = *transform,
                .visible = visibility->visible,
                .locked = lock->locked,
        });
        return inserted->second;
    };

    auto localMatrixFor = [&](EntityId entity) -> std::optional<QMat4> {
        std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state = simulatedState(entity);
        if (!state.has_value()) {
            return std::nullopt;
        }
        return composeTransform(state->get().transform.spatialTransform());
    };

    std::function<std::optional<QMat4>(EntityId, std::vector<EntityId>&)> simulatedWorldMatrix =
            [&](EntityId entity, std::vector<EntityId>& stack) -> std::optional<QMat4> {
        if (entity == InvalidEntityId) {
            return sceneTransformIdentityMatrix();
        }
        if (std::find(stack.begin(), stack.end(), entity) != stack.end()) {
            return std::nullopt;
        }
        std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state = simulatedState(entity);
        if (!state.has_value()) {
            return std::nullopt;
        }
        std::optional<QMat4> local = localMatrixFor(entity);
        if (!local.has_value()) {
            return std::nullopt;
        }
        stack.push_back(entity);
        const EntityId parent = state->get().parent;
        const std::optional<QMat4> parentWorld = parent == InvalidEntityId
                ? std::optional<QMat4>{sceneTransformIdentityMatrix()}
                : simulatedWorldMatrix(parent, stack);
        stack.pop_back();
        if (!parentWorld.has_value()) {
            return std::nullopt;
        }
        return sceneTransformMultiply(*parentWorld, *local);
    };

    auto simulatedWorldMatrixFor = [&](EntityId entity) -> std::optional<QMat4> {
        std::vector<EntityId> stack;
        return simulatedWorldMatrix(entity, stack);
    };

    auto simulatedIsDescendantOf = [&](EntityId entity, EntityId possibleAncestor) {
        if (entity == InvalidEntityId || possibleAncestor == InvalidEntityId) {
            return false;
        }
        EntityId current = entity;
        std::size_t guard = scene_.entities().size() + operations.size() + 1u;
        while (current != InvalidEntityId && guard-- > 0u) {
            if (current == possibleAncestor) {
                return true;
            }
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state = simulatedState(current);
            if (!state.has_value()) {
                return false;
            }
            current = state->get().parent;
        }
        return false;
    };

    for (const DocumentSceneBatchOperation& operation : operations) {
        if (operation.entity == InvalidEntityId || !scene_.exists(operation.entity)) {
            return failure("Batch entity does not exist.");
        }

        switch (operation.kind) {
        case DocumentSceneBatchOperationKind::Rename: {
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity cannot be renamed.");
            }
            if (state->get().name == operation.name) {
                break;
            }
            commands.push_back(std::make_unique<RenameEntityCommand>(
                    scene_,
                    operation.entity,
                    state->get().name,
                    operation.name));
            state->get().name = operation.name;
            break;
        }
        case DocumentSceneBatchOperationKind::Reparent: {
            if (operation.parent != InvalidEntityId && !scene_.exists(operation.parent)) {
                return failure("Batch parent entity does not exist.");
            }
            if (operation.parent == operation.entity || simulatedIsDescendantOf(operation.parent, operation.entity)) {
                return failure("Entity cannot be reparented");
            }
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity cannot be reparented.");
            }
            if (state->get().parent == operation.parent) {
                break;
            }

            TransformComponent afterTransform = state->get().transform;
            if (operation.preserveWorldTransform) {
                const std::optional<QMat4> childWorld = simulatedWorldMatrixFor(operation.entity);
                const std::optional<QMat4> parentWorld = simulatedWorldMatrixFor(operation.parent);
                if (!childWorld.has_value() || !parentWorld.has_value()) {
                    return failure("Batch entity cannot be reparented.");
                }
                afterTransform = transformComponentFromMatrix(
                        sceneLocalMatrixFromWorldMatrix(*parentWorld, *childWorld),
                        state->get().transform.rotationEuler);
            }

            commands.push_back(std::make_unique<ReparentEntityCommand>(
                    scene_,
                    operation.entity,
                    state->get().parent,
                    operation.parent,
                    state->get().transform,
                    afterTransform));
            state->get().parent = operation.parent;
            state->get().transform = afterTransform;
            break;
        }
        case DocumentSceneBatchOperationKind::SetTransform: {
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no transform.");
            }
            if (state->get().locked || scene_.isEffectivelyLocked(operation.entity) ||
                    documentCommandSameTransform(state->get().transform, operation.transform)) {
                break;
            }
            commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
                    scene_,
                    operation.entity,
                    state->get().transform,
                    operation.transform,
                    label));
            state->get().transform = operation.transform;
            break;
        }
        case DocumentSceneBatchOperationKind::SetVisible: {
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no visibility component.");
            }
            if (state->get().visible == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetVisibilityCommand>(
                    scene_,
                    operation.entity,
                    state->get().visible,
                    operation.flag));
            state->get().visible = operation.flag;
            break;
        }
        case DocumentSceneBatchOperationKind::SetLocked: {
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no lock component.");
            }
            if (state->get().locked == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetLockCommand>(
                    scene_,
                    operation.entity,
                    state->get().locked,
                    operation.flag));
            state->get().locked = operation.flag;
            break;
        }
        }
    }

    if (commands.empty()) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<BatchDocumentCommand>(std::move(label), std::move(commands)));
}

} // namespace quader

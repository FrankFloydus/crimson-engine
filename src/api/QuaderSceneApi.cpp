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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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

/**
 * Stores the simulated batch entity state used while converting a public scene batch to commands.
 */
struct BatchEntityState {
    std::string name;
    EntityId parent = InvalidEntityId;
    TransformComponent transform;
    bool visible = true;
    bool locked = false;
};

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
    if (mesh.mesh == InvalidAssetId || session_.assets().mesh(mesh.mesh) == nullptr) {
        return mutations_.runDocumentCommand(commandInvalidResult("Mesh asset does not exist"), "Set Mesh Component");
    }
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
    DocumentDuplicateEntityResult command = session_.documentCommands().duplicateEntity(entity);
    const EntityId duplicateEntity = command.duplicateEntity;

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

QuaderApiResult<QuaderDuplicateHierarchyResult> QuaderSceneApi::duplicateSelectedHierarchy() {
    DocumentDuplicateHierarchyResult command = session_.documentCommands().duplicateSelectedHierarchy();
    const std::vector<EntityId> duplicateRoots = command.duplicateRoots;

    QuaderApiResult<QuaderDuplicateHierarchyResult> result;
    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Duplicate Hierarchy");
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    if (result.ok && result.changed) {
        result.value.rootEntities.reserve(duplicateRoots.size());
        for (const EntityId duplicate : duplicateRoots) {
            result.value.rootEntities.push_back(QuaderEntityRef{
                    .id = duplicate,
                    .editorObjectId = session_.scene().editorObjectId(duplicate),
            });
        }
    }
    return result;
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::groupSelection(std::string groupName, TransformComponent transform) {
    DocumentGroupSelectionResult command = session_.documentCommands().groupSelection(
            std::move(groupName),
            groupTransformOrSelectedBoundsCenter(session_, transform));
    const EntityId groupEntity = command.groupEntity;

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
    std::unordered_map<EntityId, BatchEntityState> simulated;

    auto failure = [&](std::string message) {
        return mutations_.runDocumentCommand(commandInvalidResult(std::move(message)), batch.label_);
    };

    auto simulatedState = [&](EntityId entity) -> std::optional<std::reference_wrapper<BatchEntityState>> {
        if (const auto existing = simulated.find(entity); existing != simulated.end()) {
            return existing->second;
        }
        const NameComponent* name = scene.name(entity);
        const TransformComponent* transform = scene.transform(entity);
        const VisibilityComponent* visibility = scene.visibility(entity);
        const LockComponent* lock = scene.lock(entity);
        if (name == nullptr || transform == nullptr || visibility == nullptr || lock == nullptr) {
            return std::nullopt;
        }
        auto [inserted, _] = simulated.emplace(entity, BatchEntityState{
                .name = name->name,
                .parent = scene.parent(entity),
                .transform = *transform,
                .visible = visibility->visible,
                .locked = lock->locked,
        });
        return inserted->second;
    };

    for (const QuaderSceneBatch::Operation& operation : batch.operations_) {
        if (operation.entity == InvalidEntityId || !scene.exists(operation.entity)) {
            return failure("Batch entity does not exist.");
        }

        switch (operation.kind) {
        case QuaderSceneBatch::OperationKind::Rename: {
            std::optional<std::reference_wrapper<BatchEntityState>> state = simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity cannot be renamed.");
            }
            if (state->get().name == operation.name) {
                break;
            }
            commands.push_back(std::make_unique<RenameEntityCommand>(
                    scene,
                    operation.entity,
                    state->get().name,
                    operation.name));
            state->get().name = operation.name;
            break;
        }
        case QuaderSceneBatch::OperationKind::Reparent:
            if (operation.parent != InvalidEntityId && !scene.exists(operation.parent)) {
                return failure("Batch parent entity does not exist.");
            }
            if (std::optional<std::reference_wrapper<BatchEntityState>> state = simulatedState(operation.entity);
                    state.has_value()) {
                if (state->get().parent == operation.parent) {
                    break;
                }
                commands.push_back(std::make_unique<ReparentEntityCommand>(
                        scene,
                        operation.entity,
                        state->get().parent,
                        operation.parent,
                        state->get().transform,
                        state->get().transform));
                state->get().parent = operation.parent;
                break;
            }
            return failure("Batch entity cannot be reparented.");
            break;
        case QuaderSceneBatch::OperationKind::SetTransform: {
            std::optional<std::reference_wrapper<BatchEntityState>> state = simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no transform.");
            }
            if (state->get().locked || scene.isEffectivelyLocked(operation.entity) ||
                    documentCommandSameTransform(state->get().transform, operation.transform)) {
                break;
            }
            commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
                    scene,
                    operation.entity,
                    state->get().transform,
                    operation.transform,
                    batch.label_));
            state->get().transform = operation.transform;
            break;
        }
        case QuaderSceneBatch::OperationKind::SetVisible: {
            std::optional<std::reference_wrapper<BatchEntityState>> state = simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no visibility component.");
            }
            if (state->get().visible == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetVisibilityCommand>(
                    scene,
                    operation.entity,
                    state->get().visible,
                    operation.flag));
            state->get().visible = operation.flag;
            break;
        }
        case QuaderSceneBatch::OperationKind::SetLocked: {
            std::optional<std::reference_wrapper<BatchEntityState>> state = simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no lock component.");
            }
            if (state->get().locked == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetLockCommand>(
                    scene,
                    operation.entity,
                    state->get().locked,
                    operation.flag));
            state->get().locked = operation.flag;
            break;
        }
        }
    }

    if (commands.empty()) {
        return mutations_.runDocumentCommand(commandUnchangedResult(), batch.label_);
    }

    return mutations_.runDocumentCommand(
            std::make_unique<BatchDocumentCommand>(batch.label_, std::move(commands)),
            batch.label_);
}

} // namespace quader

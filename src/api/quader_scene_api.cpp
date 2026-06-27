////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_scene_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "scene/document/project_session.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "scene/scene_bounds.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}


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

TransformComponent groupTransformOrSelectedBoundsCenter(ProjectSession& session, QuaderTransform transform) {
    TransformComponent sceneTransform = toSceneTransform(transform);
    if (!isDefaultTransform(sceneTransform)) {
        return sceneTransform;
    }

    if (const std::optional<SceneBounds> bounds = selectedWorldBounds(session.scene(), session.assets())) {
        sceneTransform.position = boundsCenter(*bounds);
    }
    return sceneTransform;
}

std::vector<DocumentTransformEdit> toDocumentTransformEdits(std::span<const QuaderSceneTransformEdit> edits) {
    std::vector<DocumentTransformEdit> documentEdits;
    documentEdits.reserve(edits.size());
    for (const QuaderSceneTransformEdit& edit : edits) {
        documentEdits.push_back(DocumentTransformEdit{
                .entity = edit.entity,
                .before = toSceneTransform(edit.before),
                .after = toSceneTransform(edit.after),
        });
    }
    return documentEdits;
}

std::vector<DocumentVisibilityEdit> toDocumentVisibilityEdits(std::span<const QuaderSceneVisibilityEdit> edits) {
    std::vector<DocumentVisibilityEdit> documentEdits;
    documentEdits.reserve(edits.size());
    for (const QuaderSceneVisibilityEdit& edit : edits) {
        documentEdits.push_back(DocumentVisibilityEdit{
                .entity = edit.entity,
                .before = edit.before,
                .after = edit.after,
        });
    }
    return documentEdits;
}

std::vector<DocumentLockEdit> toDocumentLockEdits(std::span<const QuaderSceneLockEdit> edits) {
    std::vector<DocumentLockEdit> documentEdits;
    documentEdits.reserve(edits.size());
    for (const QuaderSceneLockEdit& edit : edits) {
        documentEdits.push_back(DocumentLockEdit{
                .entity = edit.entity,
                .before = edit.before,
                .after = edit.after,
        });
    }
    return documentEdits;
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

QuaderSceneBatch& QuaderSceneBatch::setTransform(EntityId entity, QuaderTransform transform) {
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

QuaderSceneApi::QuaderSceneApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::createEntity(const QuaderCreateEntityArgs& args) {
    return mutations_.createEntity(args);
}

QuaderApiVoidResult QuaderSceneApi::destroy(EntityId entity) {
    return mutations_.runDocumentResult(projectSession(session_).documentCommands().destroyEntity(entity), "Destroy Entity");
}

QuaderApiVoidResult QuaderSceneApi::rename(EntityId entity, std::string name) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().renameEntity(entity, std::move(name)),
            "Rename Entity");
}

QuaderApiVoidResult QuaderSceneApi::reparent(
        EntityId entity,
        EntityId parent,
        bool preserveWorldTransform) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().reparentEntity(entity, parent, preserveWorldTransform),
            "Reparent Entity");
}

QuaderApiVoidResult QuaderSceneApi::setTransform(EntityId entity, QuaderTransform transform) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setTransform(entity, toSceneTransform(transform)),
            "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::setTransforms(std::span<const QuaderSceneTransformEdit> edits) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setTransforms(toDocumentTransformEdits(edits)),
            "Set Transform");
}

QuaderApiVoidResult QuaderSceneApi::setVisible(EntityId entity, bool visible) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setVisible(entity, visible),
            "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::setVisibility(std::span<const QuaderSceneVisibilityEdit> edits) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setVisibility(toDocumentVisibilityEdits(edits)),
            "Set Visibility");
}

QuaderApiVoidResult QuaderSceneApi::setLocked(EntityId entity, bool locked) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setLocked(entity, locked),
            "Set Lock");
}

QuaderApiVoidResult QuaderSceneApi::setLocks(std::span<const QuaderSceneLockEdit> edits) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setLocks(toDocumentLockEdits(edits)),
            "Set Lock");
}

QuaderApiVoidResult QuaderSceneApi::setMeshComponent(EntityId entity, QuaderMeshComponent mesh) {
    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().setMeshComponent(entity, toSceneMeshComponent(mesh)),
            "Set Mesh Component");
}

QuaderApiVoidResult QuaderSceneApi::select(std::span<const EntityId> entities, EntityId active) {
    return mutations_.runDocumentResult(
            projectSession(session_).selection().setSceneSelection(entities, active),
            "Select Entities");
}

QuaderApiVoidResult QuaderSceneApi::select(std::initializer_list<EntityId> entities, EntityId active) {
    return select(std::span<const EntityId>(entities.begin(), entities.size()), active);
}

QuaderApiVoidResult QuaderSceneApi::select(EntityId entity) {
    return select({entity}, entity);
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::duplicate(EntityId entity) {
    DocumentDuplicateEntityResult command = projectSession(session_).documentCommands().duplicateEntity(entity);
    const EntityId duplicateEntity = command.duplicateEntity;

    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Duplicate Entity");
    QuaderApiResult<QuaderEntityRef> result = quaderApiResultFromStatus<QuaderEntityRef>(status);
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{
                .id = duplicateEntity,
                .editorObjectId = projectSession(session_).scene().editorObjectId(duplicateEntity),
        };
    }
    return result;
}

QuaderApiResult<QuaderDuplicateHierarchyResult> QuaderSceneApi::duplicateSelectedHierarchy() {
    DocumentDuplicateHierarchyResult command = projectSession(session_).documentCommands().duplicateSelectedHierarchy();
    const std::vector<EntityId> duplicateRoots = command.duplicateRoots;

    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Duplicate Hierarchy");
    QuaderApiResult<QuaderDuplicateHierarchyResult> result =
            quaderApiResultFromStatus<QuaderDuplicateHierarchyResult>(status);
    if (result.ok && result.changed) {
        result.value.rootEntities.reserve(duplicateRoots.size());
        for (const EntityId duplicate : duplicateRoots) {
            result.value.rootEntities.push_back(QuaderEntityRef{
                    .id = duplicate,
                    .editorObjectId = projectSession(session_).scene().editorObjectId(duplicate),
            });
        }
    }
    return result;
}

QuaderApiResult<QuaderEntityRef> QuaderSceneApi::groupSelection(std::string groupName, QuaderTransform transform) {
    DocumentGroupSelectionResult command = projectSession(session_).documentCommands().groupSelection(
            std::move(groupName),
            groupTransformOrSelectedBoundsCenter(projectSession(session_), transform));
    const EntityId groupEntity = command.groupEntity;

    const QuaderApiVoidResult status = mutations_.runDocumentResult(std::move(command), "Group Selection");
    QuaderApiResult<QuaderEntityRef> result = quaderApiResultFromStatus<QuaderEntityRef>(status);
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{
                .id = groupEntity,
                .editorObjectId = projectSession(session_).scene().editorObjectId(groupEntity),
        };
    }
    return result;
}

QuaderApiVoidResult QuaderSceneApi::ungroupSelection() {
    return mutations_.runDocumentResult(projectSession(session_).documentCommands().ungroupSelection(), "Ungroup Selection");
}

QuaderSceneBatch QuaderSceneApi::batch(std::string label) {
    return QuaderSceneBatch(*this, std::move(label));
}

QuaderApiVoidResult QuaderSceneApi::commitBatch(const QuaderSceneBatch& batch) {
    std::vector<DocumentSceneBatchOperation> operations;
    operations.reserve(batch.operations_.size());
    for (const QuaderSceneBatch::Operation& operation : batch.operations_) {
        DocumentSceneBatchOperation documentOperation{
                .entity = operation.entity,
                .parent = operation.parent,
                .preserveWorldTransform = operation.preserveWorldTransform,
                .flag = operation.flag,
                .transform = toSceneTransform(operation.transform),
                .name = operation.name,
        };
        switch (operation.kind) {
        case QuaderSceneBatch::OperationKind::Rename:
            documentOperation.kind = DocumentSceneBatchOperationKind::Rename;
            break;
        case QuaderSceneBatch::OperationKind::Reparent:
            documentOperation.kind = DocumentSceneBatchOperationKind::Reparent;
            break;
        case QuaderSceneBatch::OperationKind::SetTransform:
            documentOperation.kind = DocumentSceneBatchOperationKind::SetTransform;
            break;
        case QuaderSceneBatch::OperationKind::SetVisible:
            documentOperation.kind = DocumentSceneBatchOperationKind::SetVisible;
            break;
        case QuaderSceneBatch::OperationKind::SetLocked:
            documentOperation.kind = DocumentSceneBatchOperationKind::SetLocked;
            break;
        }
        operations.push_back(std::move(documentOperation));
    }

    return mutations_.runDocumentResult(
            projectSession(session_).documentCommands().applySceneBatch(batch.label_, std::move(operations)),
            batch.label_);
}

} // namespace quader

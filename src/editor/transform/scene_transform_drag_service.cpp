////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/scene_transform_drag_service.hpp"

#include "api/quader_editor_api.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_scene_api.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/history/editor_history_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

bool entitySelected(const SceneDocument& document, EntityId entity) {
    const SelectionComponent* selection = document.selection(entity);
    return selection != nullptr && selection->selected;
}

bool hasSelectedAncestor(const SceneDocument& document, EntityId entity) {
    EntityId parent = document.parent(entity);
    while (parent != InvalidEntityId) {
        if (entitySelected(document, parent)) {
            return true;
        }
        parent = document.parent(parent);
    }
    return false;
}

QVec3 documentLocalPivotForWorldPoint(const SceneDocument& document, EntityId entity, QVec3 worldPivot) {
    return transformPoint(inverse(document.worldTransformMatrix(entity)), worldPivot);
}

QuaderApiVoidResult commitSceneTransformPreviewThroughApi(
        QuaderEditorApi& editor,
        SceneTransformPreviewTransaction& transaction,
        const std::string& label) {
    std::vector<DocumentTransformEdit> changedEdits;
    changedEdits.reserve(transaction.edits().size());
    for (const DocumentTransformEdit& edit : transaction.edits()) {
        if (!documentCommandSameTransform(edit.before, edit.after)) {
            changedEdits.push_back(edit);
        }
    }

    const DocumentCommandResult reset = transaction.cancel();
    if (!reset.ok) {
        return {
                .ok = false,
                .changed = false,
                .message = reset.message,
                .mutationKind = QuaderMutationKind::None,
        };
    }
    if (changedEdits.empty()) {
        return {
                .ok = true,
                .changed = false,
                .message = label,
                .mutationKind = QuaderMutationKind::None,
                .revisions = editor.revisions(),
        };
    }

    QuaderSceneBatch batch = editor.scene().batch(label);
    for (const DocumentTransformEdit& edit : changedEdits) {
        batch.setTransform(edit.entity, toApiTransform(edit.after));
    }
    return batch.commit();
}

} // namespace

SceneTransformDragService::SceneTransformDragService() = default;

SceneTransformDragService::~SceneTransformDragService() = default;

SceneTransformDragService::SceneTransformDragService(SceneTransformDragService&& other) noexcept
        : preview_(std::move(other.preview_))
        , targets_(std::move(other.targets_))
        , label_(std::move(other.label_)) {
    other.preview_.reset();
}

SceneTransformDragService& SceneTransformDragService::operator=(SceneTransformDragService&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    reset();
    targets_ = std::move(other.targets_);
    label_ = std::move(other.label_);
    if (other.preview_) {
        preview_.emplace(std::move(*other.preview_));
        other.preview_.reset();
    }
    return *this;
}

bool SceneTransformDragService::begin(
        SceneDocument& document,
        QVec3 worldPivot,
        bool startPreview,
        std::string label) {
    reset();
    label_ = std::move(label);

    std::vector<EntityId> previewEntities;
    for (const EntityId entity : document.selectedEntities()) {
        if (document.isEffectivelyLocked(entity)) {
            continue;
        }
        if (hasSelectedAncestor(document, entity)) {
            continue;
        }
        if (const TransformComponent* selectedTransform = document.transform(entity)) {
            targets_.push_back(SceneTransformDragTarget{
                    .entity = entity,
                    .transform = *selectedTransform,
                    .localPivot = documentLocalPivotForWorldPoint(document, entity, worldPivot),
                    .worldTransform = document.worldTransformMatrix(entity),
            });
            previewEntities.push_back(entity);
        }
    }

    if (targets_.empty()) {
        reset();
        return false;
    }

    if (startPreview) {
        preview_.emplace(document, std::move(previewEntities), label_);
        if (preview_->empty()) {
            reset();
            return false;
        }
    }
    return true;
}

bool SceneTransformDragService::previewActive() const {
    return preview_.has_value() && preview_->active();
}

const std::vector<SceneTransformDragTarget>& SceneTransformDragService::targets() const {
    return targets_;
}

DocumentCommandResult SceneTransformDragService::previewTransform(EntityId entity, TransformComponent transform) {
    if (!preview_) {
        return documentCommandInvalidRequestResult("Transform preview is not active");
    }
    return preview_->previewTransform(entity, transform);
}

DocumentCommandResult SceneTransformDragService::previewWorldTransformDelta(
        SceneDocument& document,
        const QMat4& worldTransformDelta) {
    if (!preview_) {
        return documentCommandInvalidRequestResult("Transform preview is not active");
    }

    bool changed = false;
    for (const SceneTransformDragTarget& target : targets_) {
        if (!document.exists(target.entity)) {
            continue;
        }
        const QMat4 previewWorldTransform = sceneTransformMultiply(worldTransformDelta, target.worldTransform);
        const EntityId parent = document.parent(target.entity);
        const QMat4 parentWorldTransform = parent == InvalidEntityId
                ? sceneTransformIdentityMatrix()
                : document.worldTransformMatrix(parent);
        const TransformComponent previewTransform = transformComponentFromMatrix(
                sceneLocalMatrixFromWorldMatrix(parentWorldTransform, previewWorldTransform),
                target.transform.rotationEuler);
        const DocumentCommandResult result = preview_->previewTransform(target.entity, previewTransform);
        if (!result.ok) {
            return result;
        }
        changed = changed || result.changed;
    }
    return changed ? documentCommandChangedResult() : documentCommandUnchangedResult();
}

DocumentCommandResult SceneTransformDragService::accept() {
    if (!preview_) {
        targets_.clear();
        return documentCommandInvalidRequestResult("Transform preview is not active");
    }

    DocumentCommandResult result = preview_->accept();
    preview_.reset();
    targets_.clear();
    return result;
}

DocumentCommandResult SceneTransformDragService::commit(EditorHistoryService& history) {
    if (!preview_) {
        targets_.clear();
        return documentCommandInvalidRequestResult("Transform preview is not active");
    }

    DocumentCommandResult result = preview_->commit(history);
    preview_.reset();
    targets_.clear();
    return result;
}

QuaderApiVoidResult SceneTransformDragService::commit(QuaderEditorApi& editor) {
    if (!preview_) {
        return {
                .ok = false,
                .changed = false,
                .message = "Transform preview is not active",
                .mutationKind = QuaderMutationKind::None,
        };
    }

    QuaderApiVoidResult result = commitSceneTransformPreviewThroughApi(editor, *preview_, label_);
    preview_.reset();
    targets_.clear();
    return result;
}

DocumentCommandResult SceneTransformDragService::cancel() {
    if (!preview_) {
        targets_.clear();
        return documentCommandInvalidRequestResult("Transform preview is not active");
    }

    DocumentCommandResult result = preview_->cancel();
    preview_.reset();
    targets_.clear();
    return result;
}

void SceneTransformDragService::reset() {
    preview_.reset();
    targets_.clear();
    label_ = "Transform Selection";
}

} // namespace quader

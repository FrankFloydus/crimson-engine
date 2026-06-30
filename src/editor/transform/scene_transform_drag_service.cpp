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
#include "assets/asset_registry.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/history/editor_history_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
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

QVec3 componentScaleRatio(QVec3 next, QVec3 previous) {
    auto ratio = [](float nextValue, float previousValue) {
        const float denominator = std::abs(previousValue) <= 0.000001f ? 1.0f : previousValue;
        return nextValue / denominator;
    };
    return {ratio(next.x, previous.x), ratio(next.y, previous.y), ratio(next.z, previous.z)};
}

QVec3 multiplyComponents(QVec3 left, QVec3 right) {
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

QVec3 clampScale(QVec3 scale) {
    return {
            std::max(scale.x, 0.001f),
            std::max(scale.y, 0.001f),
            std::max(scale.z, 0.001f),
    };
}

QVec3 selectedTransformPivot(
        const SceneDocument& document,
        const AssetRegistry& assets,
        EntityId primary,
        const TransformComponent& primaryTransform) {
    if (const std::optional<SceneBounds> bounds = selectedWorldBounds(document, assets)) {
        return boundsCenter(*bounds);
    }

    if (document.exists(primary)) {
        return transformPoint(document.worldTransformMatrix(primary), {});
    }
    return primaryTransform.position;
}

void pinTargetLocalPivotToWorldPoint(
        const SceneDocument& document,
        const SceneTransformDragTarget& target,
        TransformComponent& transform,
        QVec3 worldPivot) {
    QTransform spatial = transform.spatialTransform();
    spatial.position = {};
    const EntityId parent = document.parent(target.entity);
    const QMat4 parentWorld = parent == InvalidEntityId
            ? sceneTransformIdentityMatrix()
            : document.worldTransformMatrix(parent);
    const QVec3 parentSpacePivot = transformPoint(inverse(parentWorld), worldPivot);
    transform.position = parentSpacePivot - transformPoint(composeTransform(spatial), target.localPivot);
}

template <typename BuildTransform>
std::vector<DocumentTransformEdit> buildTargetTransformEdits(
        const SceneDocument& document,
        std::span<const SceneTransformDragTarget> targets,
        BuildTransform buildTransform) {
    std::vector<DocumentTransformEdit> edits;
    edits.reserve(targets.size());
    for (const SceneTransformDragTarget& target : targets) {
        if (!document.exists(target.entity) || document.isEffectivelyLocked(target.entity)) {
            continue;
        }
        edits.push_back(DocumentTransformEdit{
                .entity = target.entity,
                .before = target.transform,
                .after = buildTransform(target),
        });
    }
    return edits;
}

QuaderApiVoidResult unchangedTransformApiResult(QuaderEditorApi& editor, const std::string& label) {
    return {
            .ok = true,
            .changed = false,
            .message = label,
            .mutationKind = QuaderMutationKind::None,
            .revisions = editor.revisions(),
    };
}

QuaderApiVoidResult commitSceneTransformEditsThroughApi(
        QuaderEditorApi& editor,
        std::span<const DocumentTransformEdit> edits,
        const std::string& label) {
    std::vector<DocumentTransformEdit> changedEdits;
    changedEdits.reserve(edits.size());
    for (const DocumentTransformEdit& edit : edits) {
        if (!documentCommandSameTransform(edit.before, edit.after)) {
            changedEdits.push_back(edit);
        }
    }
    if (changedEdits.empty()) {
        return unchangedTransformApiResult(editor, label);
    }

    QuaderSceneBatch batch = editor.scene().batch(label);
    for (const DocumentTransformEdit& edit : changedEdits) {
        batch.setTransform(edit.entity, toApiTransform(edit.after));
    }
    return batch.commit();
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

    if (!captureTargets(document, worldPivot)) {
        reset();
        return false;
    }

    if (startPreview) {
        std::vector<EntityId> previewEntities;
        previewEntities.reserve(targets_.size());
        for (const SceneTransformDragTarget& target : targets_) {
            previewEntities.push_back(target.entity);
        }
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

bool SceneTransformDragService::captureTargets(SceneDocument& document, QVec3 worldPivot) {
    targets_.clear();
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
        }
    }
    return !targets_.empty();
}

QuaderApiVoidResult SceneTransformDragService::moveSelectedToPrimaryPosition(
        QuaderEditorApi& editor,
        SceneDocument& document,
        EntityId primary,
        QVec3 nextPrimaryPosition,
        std::string label) {
    reset();
    label_ = std::move(label);
    const TransformComponent* primaryTransform = document.transform(primary);
    if (primaryTransform == nullptr) {
        return unchangedTransformApiResult(editor, label_);
    }

    const QVec3 delta = nextPrimaryPosition - primaryTransform->position;
    if (!captureTargets(document, {})) {
        reset();
        return unchangedTransformApiResult(editor, label_);
    }

    const std::vector<DocumentTransformEdit> edits = buildTargetTransformEdits(
            document,
            targets_,
            [&](const SceneTransformDragTarget& target) {
                TransformComponent next = target.transform;
                next.position = next.position + delta;
                return next;
            });
    QuaderApiVoidResult result = commitSceneTransformEditsThroughApi(editor, edits, label_);
    reset();
    return result;
}

QuaderApiVoidResult SceneTransformDragService::rotateSelectedToPrimaryEulerAroundSelectionPivot(
        QuaderEditorApi& editor,
        SceneDocument& document,
        const AssetRegistry& assets,
        EntityId primary,
        QVec3 nextPrimaryEulerRadians,
        std::string label) {
    reset();
    label_ = std::move(label);
    const TransformComponent* primaryTransform = document.transform(primary);
    if (primaryTransform == nullptr) {
        return unchangedTransformApiResult(editor, label_);
    }

    const QVec3 worldPivot = selectedTransformPivot(document, assets, primary, *primaryTransform);
    const QVec3 previousEuler = positiveEulerRadians(primaryTransform->rotationEuler);
    const QVec3 delta = nextPrimaryEulerRadians - previousEuler;
    if (!captureTargets(document, worldPivot)) {
        reset();
        return unchangedTransformApiResult(editor, label_);
    }

    const std::vector<DocumentTransformEdit> edits = buildTargetTransformEdits(
            document,
            targets_,
            [&](const SceneTransformDragTarget& target) {
                TransformComponent next = target.transform;
                next.rotationEuler = eulerRadiansAfterWorldRotationDelta(next.rotationEuler, delta);
                pinTargetLocalPivotToWorldPoint(document, target, next, worldPivot);
                return next;
            });
    QuaderApiVoidResult result = commitSceneTransformEditsThroughApi(editor, edits, label_);
    reset();
    return result;
}

QuaderApiVoidResult SceneTransformDragService::scaleSelectedToPrimaryScaleAroundSelectionPivot(
        QuaderEditorApi& editor,
        SceneDocument& document,
        const AssetRegistry& assets,
        EntityId primary,
        QVec3 nextPrimaryScale,
        std::string label) {
    reset();
    label_ = std::move(label);
    const TransformComponent* primaryTransform = document.transform(primary);
    if (primaryTransform == nullptr) {
        return unchangedTransformApiResult(editor, label_);
    }

    const QVec3 worldPivot = selectedTransformPivot(document, assets, primary, *primaryTransform);
    const QVec3 ratio = componentScaleRatio(nextPrimaryScale, primaryTransform->scale);
    if (!captureTargets(document, worldPivot)) {
        reset();
        return unchangedTransformApiResult(editor, label_);
    }

    const std::vector<DocumentTransformEdit> edits = buildTargetTransformEdits(
            document,
            targets_,
            [&](const SceneTransformDragTarget& target) {
                TransformComponent next = target.transform;
                next.scale = clampScale(multiplyComponents(next.scale, ratio));
                pinTargetLocalPivotToWorldPoint(document, target, next, worldPivot);
                return next;
            });
    QuaderApiVoidResult result = commitSceneTransformEditsThroughApi(editor, edits, label_);
    reset();
    return result;
}

DocumentCommandResult SceneTransformDragService::moveSelectedByWorldDelta(
        DocumentCommandService& documentCommands,
        SceneDocument& document,
        QVec3 worldDelta,
        std::string label) {
    const QMat4 worldTransformDelta = fromGlm(glm::translate(glm::mat4{1.0f}, toGlm(worldDelta)));
    return transformSelectedByWorldDelta(documentCommands, document, worldTransformDelta, std::move(label));
}

DocumentCommandResult SceneTransformDragService::transformSelectedByWorldDelta(
        DocumentCommandService& documentCommands,
        SceneDocument& document,
        const QMat4& worldTransformDelta,
        std::string label) {
    reset();
    label_ = label.empty() ? "Transform Selection" : std::move(label);
    if (!captureTargets(document, {})) {
        reset();
        return documentCommandUnchangedResult();
    }

    const std::vector<DocumentTransformEdit> edits = buildTargetTransformEdits(
            document,
            targets_,
            [&](const SceneTransformDragTarget& target) {
                const QMat4 nextWorldTransform = sceneTransformMultiply(worldTransformDelta, target.worldTransform);
                const EntityId parent = document.parent(target.entity);
                const QMat4 parentWorldTransform = parent == InvalidEntityId
                        ? sceneTransformIdentityMatrix()
                        : document.worldTransformMatrix(parent);
                return transformComponentFromMatrix(
                        sceneLocalMatrixFromWorldMatrix(parentWorldTransform, nextWorldTransform),
                        target.transform.rotationEuler);
            });
    DocumentCommandResult result = documentCommands.setTransforms(std::move(edits), label_);
    reset();
    return result;
}

} // namespace quader

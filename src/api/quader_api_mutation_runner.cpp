////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_mutation_runner.hpp"

#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "diagnostics/profile.hpp"
#include "scene/document/project_session.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include <memory>

#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

/**
 * Captures scene selection before history replay so modeling selection can be reprojected when needed.
 */
struct SceneSelectionSnapshot {
    std::vector<EntityId> selected;
    EntityId active = InvalidEntityId;
};

SceneSelectionSnapshot captureSceneSelection(const ProjectSession& session) {
    return {
            .selected = session.scene().selectedEntities(),
            .active = session.scene().primarySelection(),
    };
}

bool sceneSelectionChanged(const SceneSelectionSnapshot& before, const ProjectSession& session) {
    return before.selected != session.scene().selectedEntities() ||
            before.active != session.scene().primarySelection();
}

bool sceneSelectionContainsNonModelingEntity(const ProjectSession& session) {
    for (const EntityId entity : session.scene().selectedEntities()) {
        if (session.scene().modelingObject(entity) == nullptr) {
            return true;
        }
    }
    return false;
}

void projectObjectSelectionAfterHistorySelectionChange(
        ProjectSession& session,
        const SceneSelectionSnapshot& before,
        const CommandResult& result) {
    if (!result.ok || !result.changed || !sceneSelectionChanged(before, session)) {
        return;
    }

    if (sceneSelectionContainsNonModelingEntity(session)) {
        session.modeling().projectObjectSelectionFromScene(true);
        return;
    }

    const ModelingSelectionSummary summary = session.modeling().selectionSummary();
    if (!summary.hasSelection || selectionModeUsesSceneDocument(summary.mode)) {
        session.modeling().projectObjectSelectionFromScene();
    }
}

} // namespace

QuaderApiMutationRunner::QuaderApiMutationRunner(
        ProjectSession& session,
        EditorHistoryService& history,
        EditorCommandService& editorCommands,
        ModelingCommandExecutionService& modelingCommands)
    : session_(session)
    , history_(history)
    , editorCommands_(editorCommands)
    , modelingCommands_(modelingCommands) {}

QuaderRevisionStamp QuaderApiMutationRunner::currentRevisions() const {
    return quaderApiCurrentRevisions(session_);
}

EditorTransaction QuaderApiMutationRunner::beginTransaction(std::string label) {
    return history_.beginTransaction(std::move(label));
}

ModelingSessionMemento QuaderApiMutationRunner::captureModelingMemento() const {
    return session_.modeling().captureHistoryMemento();
}

QuaderApiVoidResult QuaderApiMutationRunner::restoreModelingMemento(
        const ModelingSessionMemento& memento,
        std::string_view label) {
    return statusFrom(session_.modeling().restoreHistoryMemento(memento), QuaderMutationKind::Modeling, label);
}

QuaderApiResult<QuaderEntityRef> QuaderApiMutationRunner::createEntity(const QuaderCreateEntityArgs& args) {
    DocumentCreateEntityResult command = session_.documentCommands().createEntity(
            args.name,
            toSceneEntityKind(args.kind),
            args.parent,
            toSceneTransform(args.transform));
    const EntityId createdEntity = command.entity;

    const QuaderApiVoidResult status = runDocumentResult(std::move(command), "Create Entity");
    QuaderApiResult<QuaderEntityRef> result = quaderApiResultFromStatus<QuaderEntityRef>(status);
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{
                .id = createdEntity,
                .uuid = session_.scene().uuid(createdEntity),
                .editorObjectId = session_.scene().editorObjectId(createdEntity),
        };
    }
    return result;
}

QuaderApiResult<QuaderEntityRef> QuaderApiMutationRunner::createPointLight(const QuaderCreatePointLightArgs& args) {
    DocumentCreateEntityResult command = session_.documentCommands().createPointLight(
            args.name,
            toSceneLightComponent(args.light),
            args.parent,
            toSceneTransform(args.transform),
            args.selectCreated);
    const EntityId createdEntity = command.entity;
    if (command.ok && command.changed && args.selectCreated) {
        session_.modeling().projectObjectSelectionFromScene(true);
    }

    const QuaderApiVoidResult status = runDocumentResult(std::move(command), "Create Point Light");
    QuaderApiResult<QuaderEntityRef> result = quaderApiResultFromStatus<QuaderEntityRef>(status);
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{
                .id = createdEntity,
                .uuid = session_.scene().uuid(createdEntity),
                .editorObjectId = session_.scene().editorObjectId(createdEntity),
        };
    }
    return result;
}

QuaderApiResult<QuaderEntityRef> QuaderApiMutationRunner::createDirectionalLight(
        const QuaderCreateDirectionalLightArgs& args) {
    DocumentCreateEntityResult command = session_.documentCommands().createDirectionalLight(
            args.name,
            toSceneLightComponent(args.light),
            args.parent,
            toSceneTransform(args.transform),
            args.selectCreated);
    const EntityId createdEntity = command.entity;
    if (command.ok && command.changed && args.selectCreated) {
        session_.modeling().projectObjectSelectionFromScene(true);
    }

    const QuaderApiVoidResult status = runDocumentResult(std::move(command), "Create Directional Light");
    QuaderApiResult<QuaderEntityRef> result = quaderApiResultFromStatus<QuaderEntityRef>(status);
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{
                .id = createdEntity,
                .uuid = session_.scene().uuid(createdEntity),
                .editorObjectId = session_.scene().editorObjectId(createdEntity),
        };
    }
    return result;
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentCommand(CommandResult result, std::string_view label) {
    QDR_PROFILE_FUNCTION();
    return statusFrom(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentCommand(
        std::unique_ptr<IUndoableCommand> command,
        std::string_view label) {
    QDR_PROFILE_FUNCTION();
    return statusFrom(editorCommands_.execute(std::move(command)), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentResult(DocumentCommandResult result, std::string_view label) {
    QDR_PROFILE_FUNCTION();
    return statusFrom(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runModelingResult(
        ModelingCommandResult result,
        std::string_view label) {
    QDR_PROFILE_FUNCTION();
    return statusFrom(std::move(result), QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runModelingCommand(
        std::unique_ptr<ModelingMementoCommand> command,
        std::string_view label) {
    QDR_PROFILE_FUNCTION();
    return statusFrom(editorCommands_.executeModeling(std::move(command)), QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runModelingUndoableCommand(
        std::unique_ptr<IUndoableCommand> command,
        std::string_view label) {
    QDR_PROFILE_FUNCTION();
    return statusFrom(editorCommands_.execute(std::move(command)), QuaderMutationKind::Modeling, label);
}

bool QuaderApiMutationRunner::canRunTrackedModelingTool(std::string_view toolId) const {
    return modelingCommands_.availability(
            session_.modeling(),
            toolId,
            &session_.selection()).enabled;
}

QuaderApiVoidResult QuaderApiMutationRunner::runTrackedModelingTool(std::string_view toolId) {
    QDR_PROFILE_FUNCTION();
    return statusFrom(
            modelingCommands_.execute(history_, session_.modeling(), session_.selection(), toolId),
            QuaderMutationKind::Modeling,
            toolId);
}

QuaderApiVoidResult QuaderApiMutationRunner::beginTrackedModelingPreview(std::string_view actionId) {
    return statusFrom(
            modelingCommands_.beginPreview(history_, session_.modeling(), session_.selection(), actionId),
            QuaderMutationKind::Modeling,
            actionId);
}

QuaderApiVoidResult QuaderApiMutationRunner::commitModelingPreview(std::string label) {
    return statusFrom(
            modelingCommands_.commitPreview(history_, session_.modeling(), std::move(label)),
            QuaderMutationKind::Modeling,
            "Commit Operation Preview");
}

QuaderApiVoidResult QuaderApiMutationRunner::cancelModelingPreview() {
    return statusFrom(
            modelingCommands_.cancelPreview(session_.modeling()),
            QuaderMutationKind::Runtime,
            "Cancel Operation Preview");
}

QuaderApiVoidResult QuaderApiMutationRunner::runAssetResult(bool changed, std::string_view label) {
    return quaderApiVoidStatus(session_, true, changed, {}, QuaderMutationKind::Asset, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runAssetFailure(std::string message) {
    return quaderApiFailureStatus(session_, std::move(message));
}

bool QuaderApiMutationRunner::canUndo() const {
    if (session_.modeling().canCancelActivePreviewForUndo()) {
        return true;
    }
    return history_.canUndo();
}

bool QuaderApiMutationRunner::canRedo() const {
    if (session_.modeling().canCancelActivePreviewForUndo()) {
        return false;
    }
    return history_.canRedo();
}

QuaderApiVoidResult QuaderApiMutationRunner::undo() {
    if (session_.modeling().canCancelActivePreviewForUndo()) {
        const ModelingCommandResult cancel = cancelActiveToolOrPreviewForUndo();
        if (!cancel.ok || cancel.changed || !history_.canUndo()) {
            return statusFrom(cancel, QuaderMutationKind::Runtime, "Undo");
        }
    }
    const SceneSelectionSnapshot before = captureSceneSelection(session_);
    CommandResult result = editorCommands_.undo();
    projectObjectSelectionAfterHistorySelectionChange(session_, before, result);
    return statusFrom(std::move(result), QuaderMutationKind::Document, "Undo");
}

QuaderApiVoidResult QuaderApiMutationRunner::redo() {
    if (!canRedo()) {
        return statusFrom(commandInvalidResult("Redo is not available"), QuaderMutationKind::Document, "Redo");
    }
    const SceneSelectionSnapshot before = captureSceneSelection(session_);
    CommandResult result = editorCommands_.redo();
    projectObjectSelectionAfterHistorySelectionChange(session_, before, result);
    return statusFrom(std::move(result), QuaderMutationKind::Document, "Redo");
}

QuaderApiVoidResult QuaderApiMutationRunner::repeatLastAction() {
    return statusFrom(
            editorCommands_.executeModeling(std::make_unique<RepeatLastActionHistoryCommand>(session_.modeling())),
            QuaderMutationKind::Modeling,
            "Repeat Last Action");
}

QuaderApiVoidResult QuaderApiMutationRunner::statusFrom(
        CommandResult result,
        QuaderMutationKind kind,
        std::string_view fallbackMessage) const {
    return quaderApiVoidStatus(
            session_,
            result.ok,
            result.changed,
            std::move(result.message),
            kind,
            fallbackMessage);
}

QuaderApiVoidResult QuaderApiMutationRunner::statusFrom(
        ModelingCommandResult result,
        QuaderMutationKind kind,
        std::string_view fallbackMessage) const {
    return quaderApiVoidStatus(
            session_,
            result.ok,
            result.changed,
            std::move(result.message),
            kind,
            fallbackMessage);
}

ModelingCommandResult QuaderApiMutationRunner::cancelActiveToolOrPreviewForUndo() {
    if (session_.modeling().toolSettingsTarget().operationPreviewActive) {
        return modelingCommands_.cancelPreview(session_.modeling());
    }

    ModelingCommandResult cancel = session_.modeling().cancelTransformDrag();
    if (!cancel.ok || cancel.changed) {
        return cancel;
    }

    return {.ok = true, .changed = false};
}

} // namespace quader

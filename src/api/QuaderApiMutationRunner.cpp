////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderApiMutationRunner.h"

#include "api/QuaderApiTypeConversions.h"
#include "scene/document/ProjectSession.h"
#include "editor/history/ModelingMementoCommand.h"
#include "editor/modeling/services/ModelingSessionService.h"

#include <memory>

#include <string>
#include <utility>

namespace quader {
namespace {

QuaderMutationKind changedMutationKind(bool ok, bool changed, QuaderMutationKind kind) {
    return ok && changed ? kind : QuaderMutationKind::None;
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
    const ModelingSelectionSummary modeling = session_.modeling().selectionSummary();
    return {
            .projectRevision = session_.document().revision(),
            .sceneRevision = session_.scene().revision(),
            .assetRevision = session_.assets().revision(),
            .modelingContentRevision = modeling.contentRevision,
            .modelingSelectionRevision = modeling.selectionRevision,
    };
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

QuaderApiVoidResult QuaderApiMutationRunner::recordModelingMementoTransaction(
        std::string label,
        ModelingSessionMemento before,
        ModelingSessionMemento after) {
    if (label.empty()) {
        label = "Script";
    }
    return statusFrom(
            history_.recordExecuted(std::make_unique<RecordedModelingMementoCommand>(
                    session_.modeling(),
                    std::move(label),
                    std::move(before),
                    std::move(after))),
            QuaderMutationKind::Modeling,
            "Record Script Transaction");
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
                .editorObjectId = session_.scene().editorObjectId(createdEntity),
        };
    }
    return result;
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentCommand(CommandResult result, std::string_view label) {
    return statusFrom(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentCommand(
        std::unique_ptr<IUndoableCommand> command,
        std::string_view label) {
    return statusFrom(editorCommands_.execute(std::move(command)), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentResult(DocumentCommandResult result, std::string_view label) {
    return statusFrom(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runModelingResult(
        ModelingCommandResult result,
        std::string_view label) {
    return statusFrom(std::move(result), QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runModelingCommand(
        std::unique_ptr<ModelingMementoCommand> command,
        std::string_view label) {
    return statusFrom(editorCommands_.executeModeling(std::move(command)), QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runModelingUndoableCommand(
        std::unique_ptr<IUndoableCommand> command,
        std::string_view label) {
    return statusFrom(editorCommands_.execute(std::move(command)), QuaderMutationKind::Modeling, label);
}

bool QuaderApiMutationRunner::canRunTrackedModelingTool(std::string_view toolId) const {
    return modelingCommands_.availability(
            session_.modeling(),
            toolId,
            &session_.selection()).enabled;
}

QuaderApiVoidResult QuaderApiMutationRunner::runTrackedModelingTool(std::string_view toolId) {
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
    return {
            .ok = true,
            .changed = changed,
            .message = changed ? std::string{} : std::string(label),
            .mutationKind = changed ? QuaderMutationKind::Asset : QuaderMutationKind::None,
            .revisions = currentRevisions(),
    };
}

QuaderApiVoidResult QuaderApiMutationRunner::runAssetFailure(std::string message) {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
            .revisions = currentRevisions(),
    };
}

QuaderApiVoidResult QuaderApiMutationRunner::undo() {
    return statusFrom(editorCommands_.undo(), QuaderMutationKind::Document, "Undo");
}

QuaderApiVoidResult QuaderApiMutationRunner::redo() {
    return statusFrom(editorCommands_.redo(), QuaderMutationKind::Document, "Redo");
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
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message.empty() ? std::string(fallbackMessage) : std::move(result.message),
            .mutationKind = changedMutationKind(result.ok, result.changed, kind),
            .revisions = currentRevisions(),
    };
}

QuaderApiVoidResult QuaderApiMutationRunner::statusFrom(
        ModelingCommandResult result,
        QuaderMutationKind kind,
        std::string_view fallbackMessage) const {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message.empty() ? std::string(fallbackMessage) : std::move(result.message),
            .mutationKind = changedMutationKind(result.ok, result.changed, kind),
            .revisions = currentRevisions(),
    };
}

} // namespace quader

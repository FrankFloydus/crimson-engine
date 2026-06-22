#include "api/QuaderApiMutationRunner.h"

#include "document/ProjectSession.h"
#include "editor/ModelingHistoryCommand.h"

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
        EditorCommandService& editorCommands,
        ModelingCommandExecutionService& modelingCommands)
    : session_(session)
    , editorCommands_(editorCommands)
    , modelingCommands_(modelingCommands) {}

QuaderRevisionStamp QuaderApiMutationRunner::currentRevisions() const {
    const ModelingSelectionSummary modeling = session_.modeling().selectionSummary();
    return {
            .projectRevision = session_.document().revision(),
            .sceneRevision = session_.scene().revision(),
            .assetRevision = session_.assets().revision(),
            .sdkContentRevision = modeling.contentRevision,
            .sdkSelectionRevision = modeling.selectionRevision,
    };
}

bool QuaderApiMutationRunner::canUndo() const {
    return editorCommands_.canUndo();
}

bool QuaderApiMutationRunner::canRedo() const {
    return editorCommands_.canRedo();
}

QuaderApiResult<QuaderEntityRef> QuaderApiMutationRunner::createEntity(const QuaderCreateEntityArgs& args) {
    EntityId createdEntity = InvalidEntityId;
    DocumentCommandResult command = session_.documentCommands().createEntity(
            args.name,
            args.kind,
            args.parent,
            args.transform,
            &createdEntity);

    QuaderApiResult<QuaderEntityRef> result;
    const QuaderApiVoidResult status = runDocumentResult(std::move(command), "Create Entity");
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = status.message;
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    if (result.ok && result.changed) {
        result.value = QuaderEntityRef{.id = createdEntity};
    }
    return result;
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentCommand(CommandResult result, std::string_view label) {
    return statusFrom(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runDocumentResult(DocumentCommandResult result, std::string_view label) {
    return statusFrom(std::move(result), QuaderMutationKind::Document, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runModelingResult(
        ModelingCommandResult result,
        std::string_view label) {
    return statusFrom(std::move(result), QuaderMutationKind::Modeling, label);
}

QuaderApiVoidResult QuaderApiMutationRunner::runTrackedModelingTool(std::string_view toolId) {
    return statusFrom(
            modelingCommands_.execute(session_.undoRedo(), session_.modeling(), toolId),
            QuaderMutationKind::Modeling,
            toolId);
}

QuaderApiVoidResult QuaderApiMutationRunner::runTrackedModelingAction(std::string_view actionId) {
    return statusFrom(
            modelingCommands_.executeAction(session_.undoRedo(), session_.modeling(), actionId),
            QuaderMutationKind::Modeling,
            actionId);
}

QuaderApiVoidResult QuaderApiMutationRunner::commitModelingPreview(std::string label) {
    return statusFrom(
            modelingCommands_.commitPreview(session_.undoRedo(), session_.modeling(), std::move(label)),
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

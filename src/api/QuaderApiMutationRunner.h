#pragma once

#include "api/QuaderApiResult.h"
#include "core/CommandResult.h"
#include "document/commands/DocumentCommandResult.h"
#include "editor/EditorCommandService.h"
#include "editor/ModelingCommandExecutionService.h"

#include <string>
#include <string_view>

namespace quader {

class ProjectSession;

class QuaderApiMutationRunner final {
public:
    QuaderApiMutationRunner(
            ProjectSession& session,
            EditorCommandService& editorCommands,
            ModelingCommandExecutionService& modelingCommands);

    [[nodiscard]] QuaderRevisionStamp currentRevisions() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    QuaderApiResult<QuaderEntityRef> createEntity(const QuaderCreateEntityArgs& args);
    QuaderApiVoidResult runDocumentCommand(CommandResult result, std::string_view label);
    QuaderApiVoidResult runDocumentResult(DocumentCommandResult result, std::string_view label);
    QuaderApiVoidResult runModelingResult(ModelingCommandResult result, std::string_view label);
    QuaderApiVoidResult runTrackedModelingTool(std::string_view toolId);
    QuaderApiVoidResult runTrackedModelingAction(std::string_view actionId);
    QuaderApiVoidResult commitModelingPreview(std::string label);
    QuaderApiVoidResult cancelModelingPreview();
    QuaderApiVoidResult runAssetResult(bool changed, std::string_view label);
    QuaderApiVoidResult runAssetFailure(std::string message);
    QuaderApiVoidResult undo();
    QuaderApiVoidResult redo();
    QuaderApiVoidResult repeatLastAction();

private:
    [[nodiscard]] QuaderApiVoidResult statusFrom(
            CommandResult result,
            QuaderMutationKind kind,
            std::string_view fallbackMessage = {}) const;
    [[nodiscard]] QuaderApiVoidResult statusFrom(
            ModelingCommandResult result,
            QuaderMutationKind kind,
            std::string_view fallbackMessage = {}) const;

    ProjectSession& session_;
    EditorCommandService& editorCommands_;
    ModelingCommandExecutionService& modelingCommands_;
};

} // namespace quader

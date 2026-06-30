////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "core/command_result.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/commands/editor_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/commands/modeling_command_execution_service.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace quader {

class ProjectSession;
class ModelingSessionMemento;

/**
 * Owns Quader API Mutation Runner behavior within the public Quader API surface.
 */
class QuaderApiMutationRunner final {
public:
    QuaderApiMutationRunner(
            ProjectSession& session,
            EditorHistoryService& history,
            EditorCommandService& editorCommands,
            ModelingCommandExecutionService& modelingCommands);

    [[nodiscard]] QuaderRevisionStamp currentRevisions() const;
    [[nodiscard]] EditorTransaction beginTransaction(std::string label);
    [[nodiscard]] ModelingSessionMemento captureModelingMemento() const;
    QuaderApiVoidResult restoreModelingMemento(const ModelingSessionMemento& memento, std::string_view label);

    QuaderApiResult<QuaderEntityRef> createEntity(const QuaderCreateEntityArgs& args);
    QuaderApiResult<QuaderEntityRef> createPointLight(const QuaderCreatePointLightArgs& args);
    QuaderApiResult<QuaderEntityRef> createDirectionalLight(const QuaderCreateDirectionalLightArgs& args);
    QuaderApiVoidResult runDocumentCommand(CommandResult result, std::string_view label);
    QuaderApiVoidResult runDocumentCommand(std::unique_ptr<IUndoableCommand> command, std::string_view label);
    QuaderApiVoidResult runDocumentResult(DocumentCommandResult result, std::string_view label);
    QuaderApiVoidResult runModelingResult(ModelingCommandResult result, std::string_view label);
    QuaderApiVoidResult runModelingCommand(
            std::unique_ptr<ModelingMementoCommand> command,
            std::string_view label);
    QuaderApiVoidResult runModelingUndoableCommand(
            std::unique_ptr<IUndoableCommand> command,
            std::string_view label);
    [[nodiscard]] bool canRunTrackedModelingTool(std::string_view toolId) const;
    QuaderApiVoidResult runTrackedModelingTool(std::string_view toolId);
    QuaderApiVoidResult beginTrackedModelingPreview(std::string_view actionId);
    QuaderApiVoidResult commitModelingPreview(std::string label);
    QuaderApiVoidResult cancelModelingPreview();
    QuaderApiVoidResult runAssetResult(bool changed, std::string_view label);
    QuaderApiVoidResult runAssetFailure(std::string message);
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
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
    [[nodiscard]] ModelingCommandResult cancelActiveToolOrPreviewForUndo();

    ProjectSession& session_;
    EditorHistoryService& history_;
    EditorCommandService& editorCommands_;
    ModelingCommandExecutionService& modelingCommands_;
};

} // namespace quader

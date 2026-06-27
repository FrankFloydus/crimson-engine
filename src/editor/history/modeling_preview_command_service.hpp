////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"

#include <memory>
#include <string>
#include <utility>

namespace quader {

inline ModelingCommandResult modelingCommandResultFromCommandResult(CommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

/**
 * Applies Modeling Preview Commit Command behavior as a command in the editor coordination layer.
 */
class ModelingPreviewCommitCommand final : public ModelingMementoCommand {
public:
    ModelingPreviewCommitCommand(ModelingSessionService& modeling, std::string label)
        : ModelingMementoCommand(modeling, std::move(label)) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().commitActiveOperationPreview();
    }
};

/**
 * Coordinates Modeling Preview Command Service responsibilities for the editor coordination layer.
 */
class ModelingPreviewCommandService final {
public:
    static ModelingCommandResult commit(EditorHistoryService& history, ModelingSessionService& modeling, std::string label) {
        return modelingCommandResultFromCommandResult(
                history.execute(std::make_unique<ModelingPreviewCommitCommand>(modeling, std::move(label))));
    }

    static ModelingCommandResult cancel(ModelingSessionService& modeling) {
        return modeling.cancelActiveOperationPreview();
    }
};

} // namespace quader

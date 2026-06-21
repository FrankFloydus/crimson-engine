#pragma once

#include "core/CommandResult.h"
#include "core/UndoRedoManager.h"
#include "editor/ModelingHistoryCommand.h"

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

class ModelingPreviewCommitCommand final : public ModelingHistoryCommand {
public:
    ModelingPreviewCommitCommand(ModelingSessionService& modeling, std::string label)
        : ModelingHistoryCommand(modeling, std::move(label)) {}

private:
    ModelingCommandResult executeInitial() override {
        return modeling().commitActiveOperationPreview();
    }
};

class ModelingPreviewCommandService final {
public:
    static ModelingCommandResult commit(UndoRedoManager& undoRedo, ModelingSessionService& modeling, std::string label) {
        return modelingCommandResultFromCommandResult(
                undoRedo.execute(std::make_unique<ModelingPreviewCommitCommand>(modeling, std::move(label))));
    }

    static ModelingCommandResult cancel(ModelingSessionService& modeling) {
        return modeling.cancelActiveOperationPreview();
    }
};

} // namespace quader

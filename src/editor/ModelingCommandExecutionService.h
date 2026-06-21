#pragma once

#include "core/UndoRedoManager.h"
#include "modeling/tools/ModelingCommandDispatcher.h"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

class ModelingCommandExecutionService final {
public:
    ModelingCommandResult execute(
            UndoRedoManager& undoRedo,
            ModelingSessionService& modeling,
            std::string_view toolId);

    ModelingCommandResult executeUntracked(
            ModelingSessionService& modeling,
            std::string_view toolId);

    ModelingCommandResult executeAction(
            UndoRedoManager& undoRedo,
            ModelingSessionService& modeling,
            std::string_view actionId);

    ModelingCommandResult executeActionUntracked(
            ModelingSessionService& modeling,
            std::string_view actionId);

    ModelingCommandResult commitPreview(
            UndoRedoManager& undoRedo,
            ModelingSessionService& modeling,
            std::string label);

    ModelingCommandResult cancelPreview(ModelingSessionService& modeling);

    [[nodiscard]] ModelingCommandAvailability availability(
            ModelingSessionService& modeling,
            std::string_view toolId) const;

    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy(std::string_view toolId) const;

private:
    ModelingCommandDispatcher dispatcher_;
};

[[nodiscard]] ModelingCommandResult executeModelingCommandTool(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        std::string_view toolId);
[[nodiscard]] ModelingCommandResult executeModelingCommandToolWithoutHistory(
        ModelingSessionService& modeling,
        std::string_view toolId);
[[nodiscard]] ModelingCommandResult undoModelingCommand(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling);
[[nodiscard]] ModelingCommandResult redoModelingCommand(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling);

} // namespace quader

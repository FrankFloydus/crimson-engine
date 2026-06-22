#pragma once

#include "core/UndoRedoManager.h"
#include "document/DocumentCommandService.h"
#include "editor/ModelingHistoryCommand.h"

#include <memory>
#include <utility>

namespace quader {

class EditorCommandService final {
public:
    EditorCommandService(
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
            ModelingSessionService* modeling = nullptr)
        : undoRedo_(undoRedo)
        , documentCommands_(documentCommands)
        , modeling_(modeling) {}

    CommandResult execute(std::unique_ptr<IUndoableCommand> command) {
        return undoRedo_.execute(std::move(command));
    }

    CommandResult executeModeling(std::unique_ptr<ModelingHistoryCommand> command) {
        return undoRedo_.execute(std::move(command));
    }

    CommandResult undo() {
        if (undoRedo_.canUndo()) {
            return undoRedo_.undo();
        }
        if (modeling_ != nullptr && modeling_->canUndo()) {
            return modelingHistoryCommandResult(modeling_->undo());
        }
        return commandInvalidResult("No command to undo");
    }

    CommandResult redo() {
        if (undoRedo_.canRedo()) {
            return undoRedo_.redo();
        }
        if (!undoRedo_.canUndo() && modeling_ != nullptr && modeling_->canRedo()) {
            return modelingHistoryCommandResult(modeling_->redo());
        }
        return commandInvalidResult("No command to redo");
    }

    [[nodiscard]] bool canUndo() const {
        return undoRedo_.canUndo() || (modeling_ != nullptr && modeling_->canUndo());
    }

    [[nodiscard]] bool canRedo() const {
        return undoRedo_.canRedo() ||
                (!undoRedo_.canUndo() && modeling_ != nullptr && modeling_->canRedo());
    }

    [[nodiscard]] UndoRedoManager& undoRedo() { return undoRedo_; }
    [[nodiscard]] const UndoRedoManager& undoRedo() const { return undoRedo_; }
    [[nodiscard]] DocumentCommandService& documentCommands() { return documentCommands_; }

private:
    UndoRedoManager& undoRedo_;
    DocumentCommandService& documentCommands_;
    ModelingSessionService* modeling_ = nullptr;
};

} // namespace quader

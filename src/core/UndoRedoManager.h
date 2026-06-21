#pragma once

#include "core/CommandResult.h"
#include "core/IUndoableCommand.h"

#include <memory>
#include <string>
#include <vector>

namespace quader {

class UndoRedoManager {
public:
    UndoRedoResult execute(std::unique_ptr<IUndoableCommand> command);

    UndoRedoResult undo();
    UndoRedoResult redo();

    [[nodiscard]] bool canUndo() const { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redoStack_.empty(); }
    [[nodiscard]] const std::string* undoLabel() const;
    [[nodiscard]] const std::string* redoLabel() const;

    void clear();

private:
    std::vector<std::unique_ptr<IUndoableCommand>> undoStack_;
    std::vector<std::unique_ptr<IUndoableCommand>> redoStack_;
};

} // namespace quader

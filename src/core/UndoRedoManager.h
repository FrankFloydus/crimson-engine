#pragma once

#include <functional>
#include <string>
#include <vector>

namespace quader {

struct UndoRedoResult {
    bool ok = false;
    bool changed = false;
    std::string message;
};

struct UndoRedoCommand {
    std::string label;
    std::function<UndoRedoResult()> undo;
    std::function<UndoRedoResult()> redo;
};

class UndoRedoManager {
public:
    UndoRedoResult execute(UndoRedoCommand command);
    UndoRedoResult executeExternal(
            std::string label,
            std::function<UndoRedoResult()> apply,
            std::function<UndoRedoResult()> undo,
            std::function<UndoRedoResult()> redo);

    UndoRedoResult undo();
    UndoRedoResult redo();

    [[nodiscard]] bool canUndo() const { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redoStack_.empty(); }
    [[nodiscard]] const std::string* undoLabel() const;
    [[nodiscard]] const std::string* redoLabel() const;

    void clear();

private:
    std::vector<UndoRedoCommand> undoStack_;
    std::vector<UndoRedoCommand> redoStack_;
};

} // namespace quader

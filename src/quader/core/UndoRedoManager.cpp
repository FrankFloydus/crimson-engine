#include "quader/core/UndoRedoManager.h"

#include <utility>

namespace quader {

namespace {

UndoRedoResult noUndoResult() {
    return {
            .ok = false,
            .changed = false,
            .message = "No command to undo",
    };
}

UndoRedoResult noRedoResult() {
    return {
            .ok = false,
            .changed = false,
            .message = "No command to redo",
    };
}

} // namespace

UndoRedoResult UndoRedoManager::execute(UndoRedoCommand command) {
    UndoRedoResult result = command.redo();
    if (!result.ok || !result.changed) {
        return result;
    }

    undoStack_.push_back(std::move(command));
    redoStack_.clear();
    return result;
}

UndoRedoResult UndoRedoManager::executeExternal(
        std::string label,
        std::function<UndoRedoResult()> apply,
        std::function<UndoRedoResult()> undo,
        std::function<UndoRedoResult()> redo) {
    UndoRedoResult result = apply();
    if (!result.ok || !result.changed) {
        return result;
    }

    undoStack_.push_back(UndoRedoCommand{
            .label = std::move(label),
            .undo = std::move(undo),
            .redo = std::move(redo),
    });
    redoStack_.clear();
    return result;
}

UndoRedoResult UndoRedoManager::undo() {
    if (undoStack_.empty()) {
        return noUndoResult();
    }

    UndoRedoCommand command = std::move(undoStack_.back());
    undoStack_.pop_back();
    UndoRedoResult result = command.undo();
    if (!result.ok) {
        undoStack_.push_back(std::move(command));
        return result;
    }

    redoStack_.push_back(std::move(command));
    return result;
}

UndoRedoResult UndoRedoManager::redo() {
    if (redoStack_.empty()) {
        return noRedoResult();
    }

    UndoRedoCommand command = std::move(redoStack_.back());
    redoStack_.pop_back();
    UndoRedoResult result = command.redo();
    if (!result.ok) {
        redoStack_.push_back(std::move(command));
        return result;
    }

    undoStack_.push_back(std::move(command));
    return result;
}

const std::string* UndoRedoManager::undoLabel() const {
    if (undoStack_.empty()) {
        return nullptr;
    }
    return &undoStack_.back().label;
}

const std::string* UndoRedoManager::redoLabel() const {
    if (redoStack_.empty()) {
        return nullptr;
    }
    return &redoStack_.back().label;
}

void UndoRedoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace quader

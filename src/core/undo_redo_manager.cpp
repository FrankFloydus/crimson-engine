////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/undo_redo_manager.hpp"

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

UndoRedoResult UndoRedoManager::execute(std::unique_ptr<IUndoableCommand> command) {
    return execute(std::move(command), {});
}

UndoRedoResult UndoRedoManager::execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options) {
    if (!command) {
        return {
                .ok = false,
                .changed = false,
                .message = "Command is missing",
        };
    }

    UndoRedoResult result = command->redo();
    if (!result.ok || !result.changed) {
        return result;
    }

    if (!options.mergeKey.empty() &&
            !undoStack_.empty() &&
            undoStack_.back().mergeKey == options.mergeKey &&
            undoStack_.back().command->mergeWith(*command, options.mergeKey)) {
        redoStack_.clear();
        return result;
    }

    undoStack_.push_back(StackEntry{
            .command = std::move(command),
            .mergeKey = std::move(options.mergeKey),
    });
    redoStack_.clear();
    return result;
}

UndoRedoResult UndoRedoManager::recordExecuted(std::unique_ptr<IUndoableCommand> command) {
    if (!command) {
        return {
                .ok = false,
                .changed = false,
                .message = "Command is missing",
        };
    }

    undoStack_.push_back(StackEntry{
            .command = std::move(command),
            .mergeKey = {},
    });
    redoStack_.clear();
    return commandChangedResult();
}

UndoRedoResult UndoRedoManager::undo() {
    if (undoStack_.empty()) {
        return noUndoResult();
    }

    StackEntry entry = std::move(undoStack_.back());
    undoStack_.pop_back();
    UndoRedoResult result = entry.command->undo();
    if (!result.ok) {
        undoStack_.push_back(std::move(entry));
        return result;
    }

    redoStack_.push_back(std::move(entry));
    return result;
}

UndoRedoResult UndoRedoManager::redo() {
    if (redoStack_.empty()) {
        return noRedoResult();
    }

    StackEntry entry = std::move(redoStack_.back());
    redoStack_.pop_back();
    UndoRedoResult result = entry.command->redo();
    if (!result.ok) {
        redoStack_.push_back(std::move(entry));
        return result;
    }

    undoStack_.push_back(std::move(entry));
    return result;
}

const std::string* UndoRedoManager::undoLabel() const {
    if (undoStack_.empty()) {
        return nullptr;
    }
    return &undoStack_.back().command->label();
}

const std::string* UndoRedoManager::redoLabel() const {
    if (redoStack_.empty()) {
        return nullptr;
    }
    return &redoStack_.back().command->label();
}

void UndoRedoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace quader

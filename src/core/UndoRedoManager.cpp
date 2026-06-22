////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/UndoRedoManager.h"

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

    undoStack_.push_back(std::move(command));
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

    undoStack_.push_back(std::move(command));
    redoStack_.clear();
    return commandChangedResult();
}

UndoRedoResult UndoRedoManager::undo() {
    if (undoStack_.empty()) {
        return noUndoResult();
    }

    std::unique_ptr<IUndoableCommand> command = std::move(undoStack_.back());
    undoStack_.pop_back();
    UndoRedoResult result = command->undo();
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

    std::unique_ptr<IUndoableCommand> command = std::move(redoStack_.back());
    redoStack_.pop_back();
    UndoRedoResult result = command->redo();
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
    return &undoStack_.back()->label();
}

const std::string* UndoRedoManager::redoLabel() const {
    if (redoStack_.empty()) {
        return nullptr;
    }
    return &redoStack_.back()->label();
}

void UndoRedoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace quader

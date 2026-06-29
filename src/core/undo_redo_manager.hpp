////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/i_undoable_command.hpp"
#include "core/undo_redo_execute_options.hpp"

#include <memory>
#include <string>
#include <vector>

namespace quader {

/**
 * Owns Undo Redo Manager behavior within the shared core service layer.
 */
class UndoRedoManager {
public:
    UndoRedoResult execute(std::unique_ptr<IUndoableCommand> command);
    UndoRedoResult execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options);
    UndoRedoResult recordExecuted(std::unique_ptr<IUndoableCommand> command);

    UndoRedoResult undo();
    UndoRedoResult redo();

    [[nodiscard]] bool canUndo() const { return !undoStack_.empty(); }
    [[nodiscard]] bool canRedo() const { return !redoStack_.empty(); }
    [[nodiscard]] const std::string* undoLabel() const;
    [[nodiscard]] const std::string* redoLabel() const;

    void clear();

private:
    struct StackEntry {
        std::unique_ptr<IUndoableCommand> command;
        std::string mergeKey;
    };

    std::vector<StackEntry> undoStack_;
    std::vector<StackEntry> redoStack_;
};

} // namespace quader

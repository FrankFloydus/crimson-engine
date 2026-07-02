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
    UndoRedoResult record_executed(std::unique_ptr<IUndoableCommand> command);

    UndoRedoResult undo();
    UndoRedoResult redo();

    [[nodiscard]] bool can_undo() const { return !undo_stack_.empty(); }
    [[nodiscard]] bool can_redo() const { return !redo_stack_.empty(); }
    [[nodiscard]] const std::string *undo_label() const;
    [[nodiscard]] const std::string *redo_label() const;

    void clear();

private:
    /**
     * Stores one command and its optional merge key on an undo or redo stack.
     */
    struct StackEntry {
        std::unique_ptr<IUndoableCommand> command;
        std::string merge_key;
    };

    std::vector<StackEntry> undo_stack_;
    std::vector<StackEntry> redo_stack_;
};

} // namespace quader

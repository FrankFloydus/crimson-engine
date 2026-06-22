////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/DocumentCommandService.h"
#include "editor/history/EditorHistoryService.h"
#include "editor/history/ModelingMementoCommand.h"

#include <memory>
#include <utility>

namespace quader {

/**
 * Coordinates Editor Command Service responsibilities for the editor coordination layer.
 */
class EditorCommandService final {
public:
    EditorCommandService(
            EditorHistoryService& history,
            DocumentCommandService& documentCommands)
        : history_(history)
        , documentCommands_(documentCommands) {}

    CommandResult execute(std::unique_ptr<IUndoableCommand> command) {
        return history_.execute(std::move(command));
    }

    CommandResult executeModeling(std::unique_ptr<ModelingMementoCommand> command) {
        return history_.execute(std::move(command));
    }

    CommandResult undo() {
        return history_.undo();
    }

    CommandResult redo() {
        return history_.redo();
    }

    [[nodiscard]] bool canUndo() const {
        return history_.canUndo();
    }

    [[nodiscard]] bool canRedo() const {
        return history_.canRedo();
    }

    [[nodiscard]] EditorHistoryService& history() { return history_; }
    [[nodiscard]] const EditorHistoryService& history() const { return history_; }
    [[nodiscard]] DocumentCommandService& documentCommands() { return documentCommands_; }

private:
    EditorHistoryService& history_;
    DocumentCommandService& documentCommands_;
};

} // namespace quader

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"

#include <memory>
#include <utility>

namespace quader {

/**
 * Coordinates Editor Command Service responsibilities for the editor coordination layer.
 */
class EditorCommandService final {
public:
  EditorCommandService(EditorHistoryService &history,
                       DocumentCommandService &document_commands)
      : history_(history), document_commands_(document_commands) {}

  CommandResult execute(std::unique_ptr<IUndoableCommand> command) {
    return history_.execute(std::move(command));
  }

  CommandResult
  execute_modeling(std::unique_ptr<ModelingMementoCommand> command) {
    return history_.execute(std::move(command));
  }

    CommandResult undo() {
        return history_.undo();
    }

    CommandResult redo() {
        return history_.redo();
    }

    [[nodiscard]] bool can_undo() const { return history_.can_undo(); }

    [[nodiscard]] bool can_redo() const { return history_.can_redo(); }

    [[nodiscard]] EditorHistoryService& history() { return history_; }
    [[nodiscard]] const EditorHistoryService& history() const { return history_; }
    [[nodiscard]] DocumentCommandService &document_commands() {
      return document_commands_;
    }

private:
    EditorHistoryService& history_;
    DocumentCommandService &document_commands_;
};

} // namespace quader

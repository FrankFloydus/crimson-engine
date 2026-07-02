////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_result.hpp"
#include "editor/commands/document/i_undoable_command.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace quader {

/**
 * Applies Batch Document Command behavior as a command in the editor document command layer.
 */
class BatchDocumentCommand final : public IUndoableCommand {
public:
    BatchDocumentCommand(std::string label, std::vector<std::unique_ptr<IUndoableCommand>> commands)
        : label_(std::move(label))
        , commands_(std::move(commands)) {}

    [[nodiscard]] const std::string& label() const override { return label_; }

    DocumentCommandResult undo() override {
        bool changed = false;
        std::vector<std::size_t> undone;
        for (std::size_t index = commands_.size(); index > 0u; --index) {
          const std::size_t command_index = index - 1u;
          IUndoableCommand *command = commands_[command_index].get();
          if (command == nullptr) {
            return rollback_undo(undone,
                                 document_command_missing_command_result());
          }
            DocumentCommandResult result = command->undo();
            if (!result.ok) {
              return rollback_undo(undone, std::move(result));
            }
            undone.push_back(command_index);
            changed = changed || result.changed;
        }
        return changed ? document_command_changed_result()
                       : document_command_unchanged_result();
    }

    DocumentCommandResult redo() override {
        bool changed = false;
        std::vector<std::size_t> redone;
        for (std::size_t index = 0; index < commands_.size(); ++index) {
            IUndoableCommand* command = commands_[index].get();
            if (command == nullptr) {
              return rollback_redo(redone,
                                   document_command_missing_command_result());
            }
            DocumentCommandResult result = command->redo();
            if (!result.ok) {
              return rollback_redo(redone, std::move(result));
            }
            redone.push_back(index);
            changed = changed || result.changed;
        }
        return changed ? document_command_changed_result()
                       : document_command_unchanged_result();
    }

private:
  DocumentCommandResult rollback_redo(const std::vector<std::size_t> &redone,
                                      DocumentCommandResult failure) {
    for (auto it = redone.rbegin(); it != redone.rend(); ++it) {
      IUndoableCommand *command = commands_[*it].get();
      if (command == nullptr) {
        return document_command_invalid_request_result(
            "Batch command failed and rollback command is missing");
      }
      DocumentCommandResult rollback = command->undo();
      if (!rollback.ok) {
        return document_command_invalid_request_result(
            failure.message.empty()
                ? "Batch command failed and rollback failed"
                : failure.message + "; rollback failed: " + rollback.message);
      }
    }
    return failure;
  }

  DocumentCommandResult rollback_undo(const std::vector<std::size_t> &undone,
                                      DocumentCommandResult failure) {
    for (auto it = undone.rbegin(); it != undone.rend(); ++it) {
      IUndoableCommand *command = commands_[*it].get();
      if (command == nullptr) {
        return document_command_invalid_request_result(
            "Batch undo failed and rollback command is missing");
      }
      DocumentCommandResult rollback = command->redo();
      if (!rollback.ok) {
        return document_command_invalid_request_result(
            failure.message.empty()
                ? "Batch undo failed and rollback failed"
                : failure.message + "; rollback failed: " + rollback.message);
      }
    }
    return failure;
  }

    std::string label_;
    std::vector<std::unique_ptr<IUndoableCommand>> commands_;
};

} // namespace quader

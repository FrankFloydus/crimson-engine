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

UndoRedoResult no_undo_result() {
  return {
      .ok = false,
      .changed = false,
      .message = "No command to undo",
  };
}

UndoRedoResult no_redo_result() {
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

    if (!options.merge_key.empty() && !undo_stack_.empty() &&
        undo_stack_.back().merge_key == options.merge_key &&
        undo_stack_.back().command->merge_with(*command, options.merge_key)) {
      redo_stack_.clear();
      return result;
    }

    undo_stack_.push_back(StackEntry{
        .command = std::move(command),
        .merge_key = std::move(options.merge_key),
    });
    redo_stack_.clear();
    return result;
}

UndoRedoResult
UndoRedoManager::record_executed(std::unique_ptr<IUndoableCommand> command) {
  if (!command) {
    return {
        .ok = false,
        .changed = false,
        .message = "Command is missing",
    };
  }

  undo_stack_.push_back(StackEntry{
      .command = std::move(command),
      .merge_key = {},
  });
  redo_stack_.clear();
  return command_changed_result();
}

UndoRedoResult UndoRedoManager::undo() {
  if (undo_stack_.empty()) {
    return no_undo_result();
  }

  StackEntry entry = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  UndoRedoResult result = entry.command->undo();
  if (!result.ok) {
    undo_stack_.push_back(std::move(entry));
    return result;
  }

  redo_stack_.push_back(std::move(entry));
  return result;
}

UndoRedoResult UndoRedoManager::redo() {
  if (redo_stack_.empty()) {
    return no_redo_result();
  }

  StackEntry entry = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  UndoRedoResult result = entry.command->redo();
  if (!result.ok) {
    redo_stack_.push_back(std::move(entry));
    return result;
  }

  undo_stack_.push_back(std::move(entry));
  return result;
}

const std::string *UndoRedoManager::undo_label() const {
  if (undo_stack_.empty()) {
    return nullptr;
  }
  return &undo_stack_.back().command->label();
}

const std::string *UndoRedoManager::redo_label() const {
  if (redo_stack_.empty()) {
    return nullptr;
  }
  return &redo_stack_.back().command->label();
}

void UndoRedoManager::clear() {
  undo_stack_.clear();
  redo_stack_.clear();
}

} // namespace quader

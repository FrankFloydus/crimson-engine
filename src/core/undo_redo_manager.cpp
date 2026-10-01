////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/undo_redo_manager.hpp"

#include <algorithm>
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
    ++revision_;

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
    trim_to_limit();
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
  ++revision_;
  redo_stack_.clear();
  trim_to_limit();
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
  ++revision_;
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
  ++revision_;
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

std::vector<UndoRedoHistoryEntry>
UndoRedoManager::history_entries(std::size_t limit) const {
  const std::size_t total = undo_stack_.size() + redo_stack_.size();
  const std::size_t count = std::min(limit, total);
  const bool include_original = can_restore_original_state() && count < limit;
  std::vector<UndoRedoHistoryEntry> entries;
  entries.reserve(count + (include_original ? 1u : 0u));
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t applied_count = total - index;
    const StackEntry &entry = applied_count > undo_stack_.size()
                                  ? redo_stack_[index]
                                  : undo_stack_[applied_count - 1];
    entries.push_back({.applied_count = applied_count,
                       .label = entry.command->label(),
                       .current = applied_count == undo_stack_.size()});
  }
  if (include_original) {
    entries.push_back({.applied_count = 0,
                       .label = "Original",
                       .current = undo_stack_.empty()});
  }
  return entries;
}

bool UndoRedoManager::latest_merge_key_matches(std::string_view key) const {
  return !key.empty() && redo_stack_.empty() && !undo_stack_.empty() &&
         undo_stack_.back().merge_key == key;
}

void UndoRedoManager::set_max_steps(std::size_t max_steps) {
  if (max_steps_ != max_steps) {
    ++revision_;
  }
  max_steps_ = max_steps;
  trim_to_limit();
}

void UndoRedoManager::trim_to_limit() {
  const std::size_t count = undo_stack_.size() + redo_stack_.size();
  if (count <= max_steps_) {
    return;
  }

  // Keep transitions contiguous around the current state: retire the oldest
  // applied commands first, then the most distant redo commands if necessary.
  // Never evict a command between the current state and a retained transition.
  const std::size_t excess = count - max_steps_;
  const std::size_t undo_removed = std::min(excess, undo_stack_.size());
  if (undo_removed != 0) {
    original_state_retained_ = false;
  }
  undo_stack_.erase(undo_stack_.begin(),
                    undo_stack_.begin() +
                        static_cast<std::ptrdiff_t>(undo_removed));
  const std::size_t redo_removed = excess - undo_removed;
  redo_stack_.erase(redo_stack_.begin(),
                    redo_stack_.begin() +
                        static_cast<std::ptrdiff_t>(redo_removed));
}

void UndoRedoManager::clear(UndoRedoHistoryBaseline baseline) {
  ++revision_;
  undo_stack_.clear();
  redo_stack_.clear();
  original_state_retained_ = baseline == UndoRedoHistoryBaseline::Original;
}

} // namespace quader

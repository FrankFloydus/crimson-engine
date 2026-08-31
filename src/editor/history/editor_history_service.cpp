////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/history/editor_history_service.hpp"

#include "core/undo_redo_manager.hpp"

#include <utility>

namespace quader {
namespace {

/**
 * Applies Batch Editor Command behavior as a command in the editor coordination layer.
 */
class BatchEditorCommand final : public IUndoableCommand {
public:
    BatchEditorCommand(std::string label, std::vector<std::unique_ptr<IUndoableCommand>> commands)
        : label_(std::move(label))
        , commands_(std::move(commands)) {}

    [[nodiscard]] const std::string& label() const override { return label_; }

    CommandResult undo() override {
        bool changed = false;
        std::vector<std::size_t> undone;
        for (std::size_t index = commands_.size(); index > 0u; --index) {
          const std::size_t command_index = index - 1u;
          IUndoableCommand *command = commands_[command_index].get();
          if (command == nullptr) {
            return rollback_undo(
                undone, command_invalid_result(
                            "Batch command is missing a child command"));
          }
            CommandResult result = command->undo();
            if (!result.ok) {
              return rollback_undo(undone, std::move(result));
            }
            undone.push_back(command_index);
            changed = changed || result.changed;
        }
        return changed ? command_changed_result() : command_unchanged_result();
    }

    CommandResult redo() override {
        bool changed = false;
        std::vector<std::size_t> redone;
        for (std::size_t index = 0; index < commands_.size(); ++index) {
            IUndoableCommand* command = commands_[index].get();
            if (command == nullptr) {
              return rollback_redo(
                  redone, command_invalid_result(
                              "Batch command is missing a child command"));
            }
            CommandResult result = command->redo();
            if (!result.ok) {
              return rollback_redo(redone, std::move(result));
            }
            redone.push_back(index);
            changed = changed || result.changed;
        }
        return changed ? command_changed_result() : command_unchanged_result();
    }

private:
  CommandResult rollback_redo(const std::vector<std::size_t> &redone,
                              CommandResult failure) {
    for (auto it = redone.rbegin(); it != redone.rend(); ++it) {
      IUndoableCommand *command = commands_[*it].get();
      if (command == nullptr) {
        return command_invalid_result(
            "Batch command failed and rollback command is missing");
      }
      CommandResult rollback = command->undo();
      if (!rollback.ok) {
        return command_invalid_result(
            failure.message.empty()
                ? "Batch command failed and rollback failed"
                : failure.message + "; rollback failed: " + rollback.message);
      }
    }
    return failure;
  }

  CommandResult rollback_undo(const std::vector<std::size_t> &undone,
                              CommandResult failure) {
    for (auto it = undone.rbegin(); it != undone.rend(); ++it) {
      IUndoableCommand *command = commands_[*it].get();
      if (command == nullptr) {
        return command_invalid_result(
            "Batch undo failed and rollback command is missing");
      }
      CommandResult rollback = command->redo();
      if (!rollback.ok) {
        return command_invalid_result(
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

} // namespace

EditorTransaction::EditorTransaction(EditorHistoryService& history, std::string label)
    : history_(&history)
    , label_(std::move(label)) {
  if (history.active_transaction_ != nullptr) {
    history_ = nullptr;
    return;
  }
  history.active_transaction_ = this;
}

EditorTransaction::~EditorTransaction() {
    if (history_ != nullptr) {
        const CommandResult result = cancel();
        if (!result.ok) {
          preserve_or_detach_after_failed_cancel();
        }
    }
}

EditorTransaction::EditorTransaction(EditorTransaction&& other) noexcept
    : history_(other.history_)
    , label_(std::move(other.label_))
    , commands_(std::move(other.commands_))
    , failed_cancel_restored_applied_state_(
          other.failed_cancel_restored_applied_state_) {
  if (history_ != nullptr && history_->active_transaction_ == &other) {
    history_->active_transaction_ = this;
    }
    other.history_ = nullptr;
    other.failed_cancel_restored_applied_state_ = false;
}

EditorTransaction& EditorTransaction::operator=(EditorTransaction&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (history_ != nullptr) {
        const CommandResult result = cancel();
        if (!result.ok) {
          preserve_or_detach_after_failed_cancel();
        }
    }
    history_ = other.history_;
    label_ = std::move(other.label_);
    commands_ = std::move(other.commands_);
    failed_cancel_restored_applied_state_ =
        other.failed_cancel_restored_applied_state_;
    if (history_ != nullptr && history_->active_transaction_ == &other) {
      history_->active_transaction_ = this;
    }
    other.history_ = nullptr;
    other.failed_cancel_restored_applied_state_ = false;
    return *this;
}

CommandResult EditorTransaction::execute(std::unique_ptr<IUndoableCommand> command) {
    return execute(std::move(command), {});
}

CommandResult EditorTransaction::execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options) {
    if (history_ == nullptr) {
      return command_invalid_result("Editor transaction is not active");
    }
    if (!command) {
      return command_invalid_result("Command is missing");
    }

    CommandResult result = command->redo();
    if (result.ok && result.changed) {
      if (!options.merge_key.empty() && !commands_.empty() &&
          commands_.back().merge_key == options.merge_key &&
          commands_.back().command->merge_with(*command, options.merge_key)) {
        return result;
      }
      commands_.push_back(TransactionCommand{
          .command = std::move(command),
          .merge_key = std::move(options.merge_key),
      });
    }
    return result;
}

CommandResult
EditorTransaction::record_executed(std::unique_ptr<IUndoableCommand> command) {
  if (history_ == nullptr) {
    return command_invalid_result("Editor transaction is not active");
  }
  if (!command) {
    return command_invalid_result("Command is missing");
  }

  commands_.push_back(TransactionCommand{
      .command = std::move(command),
      .merge_key = {},
  });
  return command_changed_result();
}

CommandResult EditorTransaction::record_executed_before_existing(
    std::unique_ptr<IUndoableCommand> command) {
  if (history_ == nullptr) {
    return command_invalid_result("Editor transaction is not active");
  }
  if (!command) {
    return command_invalid_result("Command is missing");
  }

  commands_.insert(commands_.begin(),
                   TransactionCommand{
                       .command = std::move(command),
                       .merge_key = {},
                   });
  return command_changed_result();
}

void EditorTransaction::set_label(std::string label) {
  if (history_ != nullptr) {
    label_ = std::move(label);
  }
}

CommandResult EditorTransaction::commit() {
    if (history_ == nullptr) {
      return command_invalid_result("Editor transaction is not active");
    }

    EditorHistoryService* history = history_;
    if (history->active_transaction_ == this) {
      history->active_transaction_ = nullptr;
    }
    history_ = nullptr;
    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(commands_.size());
    for (TransactionCommand& entry : commands_) {
        commands.push_back(std::move(entry.command));
    }
    commands_.clear();
    return history->commit_transaction(std::move(label_), std::move(commands));
}

CommandResult EditorTransaction::cancel() {
    if (history_ == nullptr) {
      return command_invalid_result("Editor transaction is not active");
    }
    EditorHistoryService *history = history_;

    failed_cancel_restored_applied_state_ = false;
    bool changed = false;
    std::vector<std::size_t> undone;
    undone.reserve(commands_.size());
    const auto rollback_undone = [this, &undone](CommandResult failure) {
      for (auto it = undone.rbegin(); it != undone.rend(); ++it) {
        IUndoableCommand *command = commands_[*it].command.get();
        if (command == nullptr) {
          return command_invalid_result(
              "Editor transaction cancel failed and rollback command is "
              "missing");
        }
        const CommandResult rollback = command->redo();
        if (!rollback.ok) {
          return command_invalid_result(
              failure.message.empty()
                  ? "Editor transaction cancel failed and rollback failed"
                  : failure.message + "; rollback failed: " +
                        rollback.message);
        }
      }
      failed_cancel_restored_applied_state_ = true;
      return failure;
    };
    for (std::size_t index = commands_.size(); index > 0u; --index) {
        const std::size_t command_index = index - 1u;
        IUndoableCommand* command = commands_[command_index].command.get();
        if (command == nullptr) {
          return rollback_undone(command_invalid_result(
              "Editor transaction command is missing"));
        }
        CommandResult result = command->undo();
        if (!result.ok) {
            return rollback_undone(std::move(result));
        }
        undone.push_back(command_index);
        changed = changed || result.changed;
    }

    commands_.clear();
    if (history->active_transaction_ == this) {
      history->active_transaction_ = nullptr;
    }
    history_ = nullptr;
    failed_cancel_restored_applied_state_ = false;
    if (changed && history->change_observer_) {
      history->change_observer_();
    }
    return changed ? command_changed_result() : command_unchanged_result();
}

void EditorTransaction::preserve_or_detach_after_failed_cancel() noexcept {
  EditorHistoryService *history = history_;
  if (history != nullptr && history->active_transaction_ == this) {
    history->active_transaction_ = nullptr;
  }
  history_ = nullptr;
  if (history == nullptr || !failed_cancel_restored_applied_state_) {
    commands_.clear();
    return;
  }

  try {
    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(commands_.size());
    for (TransactionCommand &entry : commands_) {
      commands.push_back(std::move(entry.command));
    }
    commands_.clear();
    (void)history->commit_transaction(std::move(label_), std::move(commands));
  } catch (...) {
    commands_.clear();
  }
}

CommandResult EditorTransaction::discard() {
    if (history_ == nullptr) {
      return command_invalid_result("Editor transaction is not active");
    }

    const bool changed = !commands_.empty();
    commands_.clear();
    if (history_->active_transaction_ == this) {
      history_->active_transaction_ = nullptr;
    }
    history_ = nullptr;
    return changed ? command_changed_result() : command_unchanged_result();
}

EditorHistoryService::EditorHistoryService()
    : undo_redo_(std::make_unique<UndoRedoManager>()) {}

EditorHistoryService::~EditorHistoryService() = default;

EditorHistoryService::EditorHistoryService(EditorHistoryService&&) noexcept = default;

EditorHistoryService& EditorHistoryService::operator=(EditorHistoryService&&) noexcept = default;

CommandResult EditorHistoryService::execute(std::unique_ptr<IUndoableCommand> command) {
    return execute(std::move(command), {});
}

CommandResult EditorHistoryService::execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options) {
  CommandResult result;
  if (active_transaction_ != nullptr) {
    result =
        active_transaction_->execute(std::move(command), std::move(options));
  } else {
    result = undo_redo_->execute(std::move(command), std::move(options));
  }
  if (result.ok && result.changed && change_observer_) {
    change_observer_();
  }
  return result;
}

CommandResult EditorHistoryService::record_executed(
    std::unique_ptr<IUndoableCommand> command) {
  CommandResult result;
  if (active_transaction_ != nullptr) {
    result = active_transaction_->record_executed(std::move(command));
  } else {
    result = undo_redo_->record_executed(std::move(command));
  }
  if (result.ok && result.changed && change_observer_) {
    change_observer_();
  }
  return result;
}

CommandResult EditorHistoryService::undo() {
  if (active_transaction_ != nullptr) {
    return command_invalid_result(
        "Cannot undo while an editor transaction is active");
  }
  CommandResult result = undo_redo_->undo();
  if (result.ok && result.changed && change_observer_) {
    change_observer_();
  }
  return result;
}

CommandResult EditorHistoryService::redo() {
  if (active_transaction_ != nullptr) {
    return command_invalid_result(
        "Cannot redo while an editor transaction is active");
  }
  CommandResult result = undo_redo_->redo();
  if (result.ok && result.changed && change_observer_) {
    change_observer_();
  }
  return result;
}

bool EditorHistoryService::can_undo() const {
  return active_transaction_ == nullptr && undo_redo_->can_undo();
}

bool EditorHistoryService::can_redo() const {
  return active_transaction_ == nullptr && undo_redo_->can_redo();
}

const std::string *EditorHistoryService::undo_label() const {
  return undo_redo_->undo_label();
}

const std::string *EditorHistoryService::redo_label() const {
  return undo_redo_->redo_label();
}

void EditorHistoryService::clear() { undo_redo_->clear(); }

void EditorHistoryService::set_change_observer(
    std::function<void()> observer) {
  change_observer_ = std::move(observer);
}

EditorTransaction EditorHistoryService::begin_transaction(std::string label) {
  return EditorTransaction(*this, std::move(label));
}

CommandResult EditorHistoryService::commit_transaction(
    std::string label,
    std::vector<std::unique_ptr<IUndoableCommand>> commands) {
  if (commands.empty()) {
    return command_unchanged_result();
  }
  if (label.empty()) {
    label = "Editor Transaction";
  }
  return record_executed(std::make_unique<BatchEditorCommand>(
      std::move(label), std::move(commands)));
}

} // namespace quader

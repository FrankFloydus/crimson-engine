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
            const std::size_t commandIndex = index - 1u;
            IUndoableCommand* command = commands_[commandIndex].get();
            if (command == nullptr) {
                return rollbackUndo(undone, commandInvalidResult("Batch command is missing a child command"));
            }
            CommandResult result = command->undo();
            if (!result.ok) {
                return rollbackUndo(undone, std::move(result));
            }
            undone.push_back(commandIndex);
            changed = changed || result.changed;
        }
        return changed ? commandChangedResult() : commandUnchangedResult();
    }

    CommandResult redo() override {
        bool changed = false;
        std::vector<std::size_t> redone;
        for (std::size_t index = 0; index < commands_.size(); ++index) {
            IUndoableCommand* command = commands_[index].get();
            if (command == nullptr) {
                return rollbackRedo(redone, commandInvalidResult("Batch command is missing a child command"));
            }
            CommandResult result = command->redo();
            if (!result.ok) {
                return rollbackRedo(redone, std::move(result));
            }
            redone.push_back(index);
            changed = changed || result.changed;
        }
        return changed ? commandChangedResult() : commandUnchangedResult();
    }

private:
    CommandResult rollbackRedo(const std::vector<std::size_t>& redone, CommandResult failure) {
        for (auto it = redone.rbegin(); it != redone.rend(); ++it) {
            IUndoableCommand* command = commands_[*it].get();
            if (command == nullptr) {
                return commandInvalidResult("Batch command failed and rollback command is missing");
            }
            CommandResult rollback = command->undo();
            if (!rollback.ok) {
                return commandInvalidResult(
                        failure.message.empty()
                                ? "Batch command failed and rollback failed"
                                : failure.message + "; rollback failed: " + rollback.message);
            }
        }
        return failure;
    }

    CommandResult rollbackUndo(const std::vector<std::size_t>& undone, CommandResult failure) {
        for (auto it = undone.rbegin(); it != undone.rend(); ++it) {
            IUndoableCommand* command = commands_[*it].get();
            if (command == nullptr) {
                return commandInvalidResult("Batch undo failed and rollback command is missing");
            }
            CommandResult rollback = command->redo();
            if (!rollback.ok) {
                return commandInvalidResult(
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
    if (history.activeTransaction_ != nullptr) {
        history_ = nullptr;
        return;
    }
    history.activeTransaction_ = this;
}

EditorTransaction::~EditorTransaction() {
    if (history_ != nullptr) {
        (void)cancel();
    }
}

EditorTransaction::EditorTransaction(EditorTransaction&& other) noexcept
    : history_(other.history_)
    , label_(std::move(other.label_))
    , commands_(std::move(other.commands_)) {
    if (history_ != nullptr && history_->activeTransaction_ == &other) {
        history_->activeTransaction_ = this;
    }
    other.history_ = nullptr;
}

EditorTransaction& EditorTransaction::operator=(EditorTransaction&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (history_ != nullptr) {
        (void)cancel();
    }
    history_ = other.history_;
    label_ = std::move(other.label_);
    commands_ = std::move(other.commands_);
    if (history_ != nullptr && history_->activeTransaction_ == &other) {
        history_->activeTransaction_ = this;
    }
    other.history_ = nullptr;
    return *this;
}

CommandResult EditorTransaction::execute(std::unique_ptr<IUndoableCommand> command) {
    return execute(std::move(command), {});
}

CommandResult EditorTransaction::execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options) {
    if (history_ == nullptr) {
        return commandInvalidResult("Editor transaction is not active");
    }
    if (!command) {
        return commandInvalidResult("Command is missing");
    }

    CommandResult result = command->redo();
    if (result.ok && result.changed) {
        if (!options.mergeKey.empty() &&
                !commands_.empty() &&
                commands_.back().mergeKey == options.mergeKey &&
                commands_.back().command->mergeWith(*command, options.mergeKey)) {
            return result;
        }
        commands_.push_back(TransactionCommand{
                .command = std::move(command),
                .mergeKey = std::move(options.mergeKey),
        });
    }
    return result;
}

CommandResult EditorTransaction::recordExecuted(std::unique_ptr<IUndoableCommand> command) {
    if (history_ == nullptr) {
        return commandInvalidResult("Editor transaction is not active");
    }
    if (!command) {
        return commandInvalidResult("Command is missing");
    }

    commands_.push_back(TransactionCommand{
            .command = std::move(command),
            .mergeKey = {},
    });
    return commandChangedResult();
}

CommandResult EditorTransaction::commit() {
    if (history_ == nullptr) {
        return commandInvalidResult("Editor transaction is not active");
    }

    EditorHistoryService* history = history_;
    if (history->activeTransaction_ == this) {
        history->activeTransaction_ = nullptr;
    }
    history_ = nullptr;
    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(commands_.size());
    for (TransactionCommand& entry : commands_) {
        commands.push_back(std::move(entry.command));
    }
    commands_.clear();
    return history->commitTransaction(std::move(label_), std::move(commands));
}

CommandResult EditorTransaction::cancel() {
    if (history_ == nullptr) {
        return commandInvalidResult("Editor transaction is not active");
    }

    bool changed = false;
    for (std::size_t index = commands_.size(); index > 0u; --index) {
        IUndoableCommand* command = commands_[index - 1u].command.get();
        if (command == nullptr) {
            return commandInvalidResult("Editor transaction command is missing");
        }
        CommandResult result = command->undo();
        if (!result.ok) {
            return result;
        }
        changed = changed || result.changed;
    }

    commands_.clear();
    if (history_->activeTransaction_ == this) {
        history_->activeTransaction_ = nullptr;
    }
    history_ = nullptr;
    return changed ? commandChangedResult() : commandUnchangedResult();
}

CommandResult EditorTransaction::discard() {
    if (history_ == nullptr) {
        return commandInvalidResult("Editor transaction is not active");
    }

    const bool changed = !commands_.empty();
    commands_.clear();
    if (history_->activeTransaction_ == this) {
        history_->activeTransaction_ = nullptr;
    }
    history_ = nullptr;
    return changed ? commandChangedResult() : commandUnchangedResult();
}

EditorHistoryService::EditorHistoryService()
    : undoRedo_(std::make_unique<UndoRedoManager>()) {}

EditorHistoryService::~EditorHistoryService() = default;

EditorHistoryService::EditorHistoryService(EditorHistoryService&&) noexcept = default;

EditorHistoryService& EditorHistoryService::operator=(EditorHistoryService&&) noexcept = default;

CommandResult EditorHistoryService::execute(std::unique_ptr<IUndoableCommand> command) {
    return execute(std::move(command), {});
}

CommandResult EditorHistoryService::execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options) {
    if (activeTransaction_ != nullptr) {
        return activeTransaction_->execute(std::move(command), std::move(options));
    }
    return undoRedo_->execute(std::move(command), std::move(options));
}

CommandResult EditorHistoryService::recordExecuted(std::unique_ptr<IUndoableCommand> command) {
    if (activeTransaction_ != nullptr) {
        return activeTransaction_->recordExecuted(std::move(command));
    }
    return undoRedo_->recordExecuted(std::move(command));
}

CommandResult EditorHistoryService::undo() {
    if (activeTransaction_ != nullptr) {
        return commandInvalidResult("Cannot undo while an editor transaction is active");
    }
    return undoRedo_->undo();
}

CommandResult EditorHistoryService::redo() {
    if (activeTransaction_ != nullptr) {
        return commandInvalidResult("Cannot redo while an editor transaction is active");
    }
    return undoRedo_->redo();
}

bool EditorHistoryService::canUndo() const {
    return activeTransaction_ == nullptr && undoRedo_->canUndo();
}

bool EditorHistoryService::canRedo() const {
    return activeTransaction_ == nullptr && undoRedo_->canRedo();
}

const std::string* EditorHistoryService::undoLabel() const {
    return undoRedo_->undoLabel();
}

const std::string* EditorHistoryService::redoLabel() const {
    return undoRedo_->redoLabel();
}

void EditorHistoryService::clear() {
    undoRedo_->clear();
}

EditorTransaction EditorHistoryService::beginTransaction(std::string label) {
    return EditorTransaction(*this, std::move(label));
}

CommandResult EditorHistoryService::commitTransaction(
        std::string label,
        std::vector<std::unique_ptr<IUndoableCommand>> commands) {
    if (commands.empty()) {
        return commandUnchangedResult();
    }
    if (label.empty()) {
        label = "Editor Transaction";
    }
    return recordExecuted(std::make_unique<BatchEditorCommand>(std::move(label), std::move(commands)));
}

} // namespace quader

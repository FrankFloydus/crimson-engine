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

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace quader {

class EditorHistoryService;
class UndoRedoManager;

/**
 * Owns Editor Transaction behavior within the editor coordination layer.
 */
class EditorTransaction final {
public:
    EditorTransaction(EditorHistoryService& history, std::string label);
    ~EditorTransaction();

    EditorTransaction(const EditorTransaction&) = delete;
    EditorTransaction& operator=(const EditorTransaction&) = delete;
    EditorTransaction(EditorTransaction&& other) noexcept;
    EditorTransaction& operator=(EditorTransaction&& other) noexcept;

    CommandResult execute(std::unique_ptr<IUndoableCommand> command);
    CommandResult execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options);
    CommandResult record_executed(std::unique_ptr<IUndoableCommand> command);
    CommandResult
    record_executed_before_existing(std::unique_ptr<IUndoableCommand> command);
    CommandResult commit();
    CommandResult cancel();
    CommandResult discard();

    [[nodiscard]] bool active() const { return history_ != nullptr; }
    [[nodiscard]] bool empty() const { return commands_.empty(); }

private:
    void preserve_or_detach_after_failed_cancel() noexcept;

    /**
     * Stores a transaction child command with the merge key used when it was queued.
     */
    struct TransactionCommand {
        std::unique_ptr<IUndoableCommand> command;
        std::string merge_key;
    };

    EditorHistoryService* history_ = nullptr;
    std::string label_;
    std::vector<TransactionCommand> commands_;
    bool failed_cancel_restored_applied_state_ = false;
};

/**
 * Coordinates Editor History Service responsibilities for the editor coordination layer.
 */
class EditorHistoryService final {
public:
    EditorHistoryService();
    ~EditorHistoryService();

    EditorHistoryService(const EditorHistoryService&) = delete;
    EditorHistoryService& operator=(const EditorHistoryService&) = delete;
    EditorHistoryService(EditorHistoryService&&) noexcept;
    EditorHistoryService& operator=(EditorHistoryService&&) noexcept;

    CommandResult execute(std::unique_ptr<IUndoableCommand> command);
    CommandResult execute(std::unique_ptr<IUndoableCommand> command, UndoRedoExecuteOptions options);
    CommandResult record_executed(std::unique_ptr<IUndoableCommand> command);
    CommandResult undo();
    CommandResult redo();

    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    [[nodiscard]] const std::string *undo_label() const;
    [[nodiscard]] const std::string *redo_label() const;

    void clear();
    void set_change_observer(std::function<void()> observer);

    [[nodiscard]] EditorTransaction begin_transaction(std::string label);

  private:
    friend class EditorTransaction;

    CommandResult
    commit_transaction(std::string label,
                       std::vector<std::unique_ptr<IUndoableCommand>> commands);

    std::unique_ptr<UndoRedoManager> undo_redo_;
    EditorTransaction *active_transaction_ = nullptr;
    std::function<void()> change_observer_;
};

} // namespace quader

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
#include "core/undo_redo_history_entry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
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
    [[nodiscard]] std::size_t undo_count() const { return undo_stack_.size(); }
    [[nodiscard]] std::size_t redo_count() const { return redo_stack_.size(); }
    [[nodiscard]] std::size_t max_steps() const { return max_steps_; }
    /// Monotonic history epoch, including undo/redo even when the head returns.
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    /// Copies at most limit retained states, newest first, including Original
    /// while its complete applied transition prefix remains reachable.
    [[nodiscard]] std::vector<UndoRedoHistoryEntry>
    history_entries(std::size_t limit) const;
    [[nodiscard]] bool can_restore_original_state() const {
      return original_state_retained_ && max_steps_ != 0;
    }
    /// Tests the latest applied command without executing or changing history.
    [[nodiscard]] bool latest_merge_key_matches(std::string_view key) const;

    // Bounds the combined retained undo/redo chain. Zero disables retention.
    // Pruning releases commands without executing them or changing live state.
    void set_max_steps(std::size_t max_steps);

    void clear(UndoRedoHistoryBaseline baseline =
                   UndoRedoHistoryBaseline::Original);

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
    std::size_t max_steps_ = std::numeric_limits<std::size_t>::max();
    std::uint64_t revision_ = 0;
    bool original_state_retained_ = true;

    void trim_to_limit();
};

} // namespace quader

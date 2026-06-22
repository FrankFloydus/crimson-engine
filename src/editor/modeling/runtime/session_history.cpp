////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.h"

#include <algorithm>
#include <utility>

namespace quader_editor_core {

HistoryStatus EditorSession::history_status() const
{
	return {
		!undo_stack_.empty(),
		!redo_stack_.empty(),
		saturated_history_size(undo_stack_.size()),
		saturated_history_size(redo_stack_.size()),
	};
}

void EditorSession::clear_history()
{
	undo_stack_.clear();
	redo_stack_.clear();
}

void EditorSession::record_history(SessionState before)
{
	const SessionState after = state_;
	if (before.content_revision == after.content_revision && before.selection_revision == after.selection_revision) {
		return;
	}

	undo_stack_.push_back({ std::move(before), after });
	while (undo_stack_.size() > editor_history_default_max_depth) {
		undo_stack_.erase(undo_stack_.begin());
	}
	redo_stack_.clear();
}

HistoryRestoreResult EditorSession::undo()
{
	if (undo_stack_.empty()) {
		return { false, history_status() };
	}

	HistoryEntry entry = std::move(undo_stack_.back());
	undo_stack_.pop_back();
	restore_recorded_state(entry.before);
	redo_stack_.push_back(std::move(entry));
	return { true, history_status() };
}

HistoryRestoreResult EditorSession::redo()
{
	if (redo_stack_.empty()) {
		return { false, history_status() };
	}

	HistoryEntry entry = std::move(redo_stack_.back());
	redo_stack_.pop_back();
	restore_recorded_state(entry.after);
	undo_stack_.push_back(std::move(entry));
	return { true, history_status() };
}

} // namespace quader_editor_core

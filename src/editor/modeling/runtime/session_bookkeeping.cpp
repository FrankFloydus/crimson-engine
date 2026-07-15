////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace quader_editor_core {

std::uint64_t advance_session_revision(std::uint64_t revision) noexcept
{
	return revision == (std::numeric_limits<std::uint64_t>::max)()
		? revision
		: revision + 1U;
}

std::uint64_t restored_session_revision(std::uint64_t recorded,
                                        std::uint64_t current) noexcept
{
	return std::max(recorded, advance_session_revision(current));
}

void EditorSession::normalize_active_object()
{
	normalize_active_object_id(state_.objects, state_.active_object_id);
	normalize_active_light_id(state_.lights, state_.active_light_id);
}

void EditorSession::note_selection_changed()
{
	state_.selection_revision =
		advance_session_revision(state_.selection_revision);
}

void EditorSession::note_content_result(const CommandResult& result)
{
	if (!result.ok || !result.changed) {
		return;
	}
	state_.dirty = true;
	state_.content_revision = advance_session_revision(state_.content_revision);
}

void EditorSession::restore_history_state(SessionState state)
{
	const int next_object_id = state_.next_object_id;
	const int next_light_id = state_.next_light_id;
	const std::uint64_t content_revision = state_.content_revision;
	const std::uint64_t selection_revision = state_.selection_revision;
	state.next_object_id = std::max(state.next_object_id, next_object_id);
	state.next_light_id = std::max(state.next_light_id, next_light_id);
	state.content_revision =
		restored_session_revision(state.content_revision,
		                          content_revision);
	state.selection_revision =
		restored_session_revision(state.selection_revision,
		                          selection_revision);
	set_state(std::move(state));
}

} // namespace quader_editor_core

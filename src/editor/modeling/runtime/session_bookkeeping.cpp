////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <algorithm>
#include <utility>

namespace quader_editor_core {

void EditorSession::normalize_active_object()
{
	normalize_active_object_id(state_.objects, state_.active_object_id);
	normalize_active_light_id(state_.lights, state_.active_light_id);
}

void EditorSession::note_selection_changed()
{
	++state_.selection_revision;
}

void EditorSession::note_content_result(const CommandResult& result)
{
	if (!result.ok || !result.changed) {
		return;
	}
	state_.dirty = true;
	++state_.content_revision;
}

void EditorSession::restore_recorded_state(SessionState state)
{
	const SessionState current = state_;
	state.content_revision = std::max(state.content_revision, current.content_revision + 1U);
	state.selection_revision = std::max(state.selection_revision, current.selection_revision + 1U);
	set_state(std::move(state));
}

} // namespace quader_editor_core

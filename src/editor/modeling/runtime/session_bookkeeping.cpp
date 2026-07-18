////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <limits>

namespace quader_editor_core {

std::uint64_t advance_session_revision(std::uint64_t revision) noexcept {
  return revision == (std::numeric_limits<std::uint64_t>::max)()
             ? revision
             : revision + 1U;
}

void EditorSession::normalize_active_object() {
  normalize_active_object_id(state_.objects, state_.active_object_id);
}

void EditorSession::note_selection_changed() {
  state_.selection_revision =
      advance_session_revision(state_.selection_revision);
}

} // namespace quader_editor_core

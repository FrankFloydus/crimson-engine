////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "session_internal.hpp"

#include <utility>

namespace quader_editor_core {

EditorSession::EditorSession(std::vector<PolygonObject> objects) {
  state_.objects = std::move(objects);
  normalize_active_object();
}

const SessionState &EditorSession::state() const { return state_; }

SessionState &EditorSession::state() { return state_; }

const std::vector<PolygonObject> &EditorSession::objects() const {
  return state_.objects;
}

std::vector<PolygonObject> &EditorSession::objects() { return state_.objects; }

int EditorSession::active_object_id() const { return state_.active_object_id; }

SelectionMode EditorSession::selection_mode() const {
  return state_.selection_mode;
}

} // namespace quader_editor_core

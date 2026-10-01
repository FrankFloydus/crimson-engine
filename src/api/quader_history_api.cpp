////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_history_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_mutation_runner.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"

namespace quader {
namespace {

const SceneSession &read_scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_scene_session(session);
}

} // namespace

QuaderHistoryApi::QuaderHistoryApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiVoidResult QuaderHistoryApi::undo() {
    return mutations_.undo();
}

QuaderApiVoidResult QuaderHistoryApi::redo() {
    return mutations_.redo();
}

QuaderApiVoidResult QuaderHistoryApi::repeat_last_action() {
  return mutations_.repeat_last_action();
}

bool QuaderHistoryApi::can_undo() const { return mutations_.can_undo(); }

bool QuaderHistoryApi::can_redo() const { return mutations_.can_redo(); }

QuaderHistorySnapshot QuaderHistoryApi::snapshot() const {
    QuaderHistorySnapshot result;
    const SceneSession &session = read_scene_session(session_);
    if (const auto local_history =
            session.native_tools().active_history_state()) {
      result.can_undo = local_history->can_undo;
      result.can_redo = local_history->can_redo;
      if (result.can_undo) {
        result.undo_label = local_history->label;
      }
      if (result.can_redo) {
        result.redo_label = local_history->label;
      }
      return result;
    }
    result.can_undo = can_undo();
    result.can_redo = can_redo();
    if (const std::string *label =
            session.history().undo_label()) {
      result.undo_label = *label;
    }
    if (const std::string *label =
            session.history().redo_label()) {
      result.redo_label = *label;
    }
    return result;
}

QuaderUndoHistorySnapshot
QuaderHistoryApi::undo_history(std::size_t limit) const {
  QuaderUndoHistorySnapshot result;
  result.unavailable_reason = mutations_.history_restore_unavailable_reason();
  if (!mutations_.available()) {
    return result;
  }
  const SceneSession &session = read_scene_session(session_);
  result.scene_session_id = mutations_.scene_session_id();
  result.document_binding_generation = session.document_binding_generation();
  result.history_revision = session.history().revision();
  result.can_restore = result.unavailable_reason.empty();
  for (const UndoRedoHistoryEntry &entry :
       session.history().history_entries(limit)) {
    result.entries.push_back({.applied_count = entry.applied_count,
                              .label = entry.label,
                              .current = entry.current});
  }
  return result;
}

QuaderApiVoidResult QuaderHistoryApi::restore_history(
    const QuaderUndoHistorySnapshot &snapshot, std::size_t applied_count) {
  return mutations_.restore_history(snapshot, applied_count);
}

} // namespace quader

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
#include "scene/document/project_session.hpp"

namespace quader {
namespace {

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
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
    result.can_undo = can_undo();
    result.can_redo = can_redo();
    if (const std::string *label =
            project_session(session_).history().undo_label()) {
      result.undo_label = *label;
    }
    if (const std::string *label =
            project_session(session_).history().redo_label()) {
      result.redo_label = *label;
    }
    return result;
}

} // namespace quader

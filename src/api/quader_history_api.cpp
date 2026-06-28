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

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
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

QuaderApiVoidResult QuaderHistoryApi::repeatLastAction() {
    return mutations_.repeatLastAction();
}

bool QuaderHistoryApi::canUndo() const {
    return mutations_.canUndo();
}

bool QuaderHistoryApi::canRedo() const {
    return projectSession(session_).history().canRedo();
}

QuaderHistorySnapshot QuaderHistoryApi::snapshot() const {
    QuaderHistorySnapshot result;
    result.canUndo = canUndo();
    result.canRedo = canRedo();
    if (const std::string* label = projectSession(session_).history().undoLabel()) {
        result.undoLabel = *label;
    }
    if (const std::string* label = projectSession(session_).history().redoLabel()) {
        result.redoLabel = *label;
    }
    return result;
}

} // namespace quader

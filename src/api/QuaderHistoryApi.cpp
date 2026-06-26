////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderHistoryApi.h"
#include "api/QuaderApiSessionInternalAccess.h"

#include "api/QuaderApiMutationRunner.h"
#include "editor/history/EditorHistoryService.h"
#include "scene/document/ProjectSession.h"

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
    return projectSession(session_).history().canUndo();
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

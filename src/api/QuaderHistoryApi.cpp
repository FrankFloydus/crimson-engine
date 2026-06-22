////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderHistoryApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "editor/history/EditorHistoryService.h"

#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

QuaderApiVoidResult historyStatusFrom(
        CommandResult result,
        const QuaderApiMutationRunner& mutations,
        std::string_view fallbackMessage) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message.empty() ? std::string(fallbackMessage) : std::move(result.message),
            .mutationKind = result.ok && result.changed ? QuaderMutationKind::Document : QuaderMutationKind::None,
            .revisions = mutations.currentRevisions(),
    };
}

} // namespace

QuaderHistoryApi::QuaderHistoryApi(EditorHistoryService& history, QuaderApiMutationRunner& mutations)
    : history_(history)
    , mutations_(mutations) {}

QuaderApiVoidResult QuaderHistoryApi::undo() {
    return historyStatusFrom(history_.undo(), mutations_, "Undo");
}

QuaderApiVoidResult QuaderHistoryApi::redo() {
    return historyStatusFrom(history_.redo(), mutations_, "Redo");
}

QuaderApiVoidResult QuaderHistoryApi::repeatLastAction() {
    return mutations_.repeatLastAction();
}

bool QuaderHistoryApi::canUndo() const {
    return history_.canUndo();
}

bool QuaderHistoryApi::canRedo() const {
    return history_.canRedo();
}

QuaderHistorySnapshot QuaderHistoryApi::snapshot() const {
    QuaderHistorySnapshot result;
    result.canUndo = canUndo();
    result.canRedo = canRedo();
    if (const std::string* label = history_.undoLabel()) {
        result.undoLabel = *label;
    }
    if (const std::string* label = history_.redoLabel()) {
        result.redoLabel = *label;
    }
    return result;
}

} // namespace quader

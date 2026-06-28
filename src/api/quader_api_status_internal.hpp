////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/document/project_session.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

inline QuaderMutationKind quaderApiChangedMutationKind(
        bool ok,
        bool changed,
        QuaderMutationKind changedKind) {
    return ok && changed ? changedKind : QuaderMutationKind::None;
}

inline QuaderRevisionStamp quaderApiCurrentRevisions(const ProjectSession& session) {
    const ModelingSelectionSummary modeling = session.modeling().selectionSummary();
    return {
            .projectRevision = session.document().revision(),
            .sceneRevision = session.scene().revision(),
            .assetRevision = session.assets().revision(),
            .modelingContentRevision = modeling.contentRevision,
            .modelingSelectionRevision = modeling.selectionRevision,
    };
}

inline QuaderApiVoidResult quaderApiVoidStatus(
        const ProjectSession& session,
        bool ok,
        bool changed,
        std::string message,
        QuaderMutationKind changedKind,
        std::string_view fallbackMessage = {}) {
    return {
            .ok = ok,
            .changed = changed,
            .message = message.empty() ? std::string(fallbackMessage) : std::move(message),
            .mutationKind = quaderApiChangedMutationKind(ok, changed, changedKind),
            .revisions = quaderApiCurrentRevisions(session),
    };
}

inline QuaderApiVoidResult quaderApiFailureStatus(
        const ProjectSession& session,
        std::string message) {
    return quaderApiVoidStatus(
            session,
            false,
            false,
            std::move(message),
            QuaderMutationKind::None);
}

inline QuaderMutationKind quaderApiPreviewMutationKind(
        bool ok,
        bool changed,
        bool active,
        QuaderMutationKind changedKind) {
    if (!ok) {
        return QuaderMutationKind::None;
    }
    if (changed) {
        return changedKind;
    }
    return active ? QuaderMutationKind::Runtime : QuaderMutationKind::None;
}

} // namespace quader

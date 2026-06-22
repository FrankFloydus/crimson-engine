////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderProjectApi.h"

#include "document/ProjectSession.h"

#include <utility>

namespace quader {

QuaderProjectApi::QuaderProjectApi(ProjectSession& session)
    : session_(session)
    , savedRevision_(session.document().revision()) {}

QuaderApiVoidResult QuaderProjectApi::newProject() {
    return unsupported("Project replacement is not implemented until ProjectSession owns a reset/open lifecycle.");
}

QuaderApiVoidResult QuaderProjectApi::open(const std::filesystem::path& path) {
    (void)path;
    return unsupported("Project open is not implemented until project IO services are available.");
}

QuaderApiVoidResult QuaderProjectApi::save() {
    return unsupported("Project save is not implemented until project IO services are available.");
}

QuaderApiVoidResult QuaderProjectApi::saveAs(const std::filesystem::path& path) {
    (void)path;
    return unsupported("Project save-as is not implemented until project IO services are available.");
}

QuaderApiVoidResult QuaderProjectApi::close() {
    return unsupported("Project close is not implemented until ProjectSession owns a close lifecycle.");
}

bool QuaderProjectApi::dirty() const {
    return session_.document().revision() != savedRevision_;
}

const std::filesystem::path& QuaderProjectApi::path() const {
    return path_;
}

QuaderProjectMetadata QuaderProjectApi::metadata() const {
    return {
            .id = session_.document().id(),
            .name = session_.document().name(),
    };
}

QuaderApiVoidResult QuaderProjectApi::unsupported(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
            .revisions = {
                    .projectRevision = session_.document().revision(),
                    .sceneRevision = session_.scene().revision(),
                    .assetRevision = session_.assets().revision(),
            },
    };
}

} // namespace quader

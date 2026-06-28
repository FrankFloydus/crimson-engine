////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_project_api.hpp"

#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "scene/document/project_session.hpp"

#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}

} // namespace

QuaderProjectApi::QuaderProjectApi(QuaderApiSession session)
    : session_(session) {}

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
    return projectSession(session_).dirty();
}

const std::filesystem::path& QuaderProjectApi::path() const {
    return path_;
}

QuaderProjectMetadata QuaderProjectApi::metadata() const {
    ProjectSession& session = projectSession(session_);
    return {
            .id = session.document().id(),
            .name = session.document().name(),
    };
}

QuaderApiVoidResult QuaderProjectApi::unsupported(std::string message) const {
    return quaderApiFailureStatus(projectSession(session_), std::move(message));
}

} // namespace quader

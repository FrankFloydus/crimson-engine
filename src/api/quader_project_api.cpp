////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_project_api.hpp"

#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "diagnostics/profile.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "scene/document/project_session.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}

QuaderApiVoidResult projectRuntimeStatus(
        ProjectSession& session,
        bool changed,
        std::string message) {
    return quaderApiVoidStatus(
            session,
            true,
            changed,
            std::move(message),
            QuaderMutationKind::Runtime);
}

} // namespace

QuaderProjectApi::QuaderProjectApi(QuaderApiSession session)
    : session_(session) {}

QuaderApiVoidResult QuaderProjectApi::newProject() {
    ProjectSession& session = projectSession(session_);
    session.replaceDocument(ProjectDocument{});
    path_.clear();
    return projectRuntimeStatus(session, true, "New project created.");
}

QuaderApiVoidResult QuaderProjectApi::open(const std::filesystem::path& path) {
    std::filesystem::path target = path;
    if (target.empty()) {
        if (!runtimeHooks_.chooseOpenPath) {
            return unsupported("Project open requires a path.");
        }
        const std::optional<std::filesystem::path> selected = chooseOpenPath();
        if (!selected.has_value()) {
            return projectRuntimeStatus(projectSession(session_), false, "Project open canceled.");
        }
        target = *selected;
    }
    if (target.empty()) {
        return unsupported("Project open requires a path.");
    }

    QdrProjectReadResult read = QdrDocumentReader::readProjectDocument(target);
    if (!read.ok) {
        return quaderApiFailureStatus(
                projectSession(session_),
                read.message.empty() ? "Project open failed." : std::move(read.message));
    }

    ProjectSession& session = projectSession(session_);
    session.replaceDocument(std::move(read.document));
    path_ = std::move(target);
    return projectRuntimeStatus(
            session,
            true,
            read.migratedLegacySession ? "Legacy QDR project opened." : "Project opened.");
}

QuaderApiVoidResult QuaderProjectApi::save() {
    QDR_PROFILE_FUNCTION();
    if (path_.empty()) {
        return saveAs({});
    }
    return saveToPath(path_, false);
}

QuaderApiVoidResult QuaderProjectApi::saveAs(const std::filesystem::path& path) {
    QDR_PROFILE_FUNCTION();
    std::filesystem::path target = path;
    if (target.empty()) {
        if (!runtimeHooks_.chooseSaveAsPath) {
            return unsupported("Project save-as requires a path.");
        }
        const std::optional<std::filesystem::path> selected = chooseSaveAsPath();
        if (!selected.has_value()) {
            return projectRuntimeStatus(projectSession(session_), false, "Project save-as canceled.");
        }
        target = *selected;
    }
    if (target.empty()) {
        return unsupported("Project save-as requires a path.");
    }

    return saveToPath(target, true);
}

QuaderApiVoidResult QuaderProjectApi::close() {
    return newProject();
}

void QuaderProjectApi::setRuntimeHooks(QuaderProjectRuntimeHooks hooks) {
    runtimeHooks_ = std::move(hooks);
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

QuaderApiVoidResult QuaderProjectApi::saveToPath(const std::filesystem::path& path, bool updatePath) {
    QDR_PROFILE_FUNCTION();
    if (path.empty()) {
        return unsupported("Project save requires a path.");
    }

    ProjectSession& session = projectSession(session_);
    QdrProjectWriteResult write = [&]() {
        QDR_PROFILE_SCOPE("Project write QDR document");
        return QdrDocumentWriter::writeProjectDocumentAtomic(path, session.document());
    }();
    if (!write.ok) {
        return quaderApiFailureStatus(
                session,
                write.message.empty() ? "Project save failed." : std::move(write.message));
    }

    if (updatePath) {
        path_ = path;
    }
    session.markClean();
    return projectRuntimeStatus(session, true, "Project saved.");
}

std::optional<std::filesystem::path> QuaderProjectApi::chooseOpenPath() const {
    return runtimeHooks_.chooseOpenPath ? runtimeHooks_.chooseOpenPath() : std::nullopt;
}

std::optional<std::filesystem::path> QuaderProjectApi::chooseSaveAsPath() const {
    return runtimeHooks_.chooseSaveAsPath ? runtimeHooks_.chooseSaveAsPath() : std::nullopt;
}

} // namespace quader

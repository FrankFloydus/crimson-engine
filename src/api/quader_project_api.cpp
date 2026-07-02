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

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
}

QuaderApiVoidResult project_runtime_status(ProjectSession &session,
                                           bool changed, std::string message) {
  return quader_api_void_status(session, true, changed, std::move(message),
                                QuaderMutationKind::Runtime);
}

} // namespace

QuaderProjectApi::QuaderProjectApi(QuaderApiSession session)
    : session_(session) {}

QuaderApiVoidResult QuaderProjectApi::new_project() {
  ProjectSession &session = project_session(session_);
  session.replace_document(ProjectDocument{});
  path_.clear();
  return project_runtime_status(session, true, "New project created.");
}

QuaderApiVoidResult QuaderProjectApi::open(const std::filesystem::path& path) {
    std::filesystem::path target = path;
    if (target.empty()) {
      if (!runtime_hooks_.choose_open_path) {
        return unsupported("Project open requires a path.");
      }
      const std::optional<std::filesystem::path> selected = choose_open_path();
      if (!selected.has_value()) {
        return project_runtime_status(project_session(session_), false,
                                      "Project open canceled.");
      }
        target = *selected;
    }
    if (target.empty()) {
        return unsupported("Project open requires a path.");
    }

    QdrProjectReadResult read =
        QdrDocumentReader::read_project_document(target);
    if (!read.ok) {
      return quader_api_failure_status(project_session(session_),
                                       read.message.empty()
                                           ? "Project open failed."
                                           : std::move(read.message));
    }

    ProjectSession &session = project_session(session_);
    session.replace_document(std::move(read.document));
    path_ = std::move(target);
    return project_runtime_status(session, true,
                                  read.migrated_legacy_session
                                      ? "Legacy QDR project opened."
                                      : "Project opened.");
}

QuaderApiVoidResult QuaderProjectApi::save() {
    QDR_PROFILE_FUNCTION();
    if (path_.empty()) {
      return save_as({});
    }
    return save_to_path(path_, false);
}

QuaderApiVoidResult
QuaderProjectApi::save_as(const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  std::filesystem::path target = path;
  if (target.empty()) {
    if (!runtime_hooks_.choose_save_as_path) {
      return unsupported("Project save-as requires a path.");
    }
    const std::optional<std::filesystem::path> selected = choose_save_as_path();
    if (!selected.has_value()) {
      return project_runtime_status(project_session(session_), false,
                                    "Project save-as canceled.");
    }
    target = *selected;
  }
  if (target.empty()) {
    return unsupported("Project save-as requires a path.");
  }

  return save_to_path(target, true);
}

QuaderApiVoidResult QuaderProjectApi::close() { return new_project(); }

void QuaderProjectApi::set_runtime_hooks(QuaderProjectRuntimeHooks hooks) {
  runtime_hooks_ = std::move(hooks);
}

bool QuaderProjectApi::dirty() const {
  return project_session(session_).dirty();
}

const std::filesystem::path& QuaderProjectApi::path() const {
    return path_;
}

QuaderProjectMetadata QuaderProjectApi::metadata() const {
  ProjectSession &session = project_session(session_);
  return {
      .id = session.document().id(),
      .name = session.document().name(),
  };
}

QuaderApiVoidResult QuaderProjectApi::unsupported(std::string message) const {
  return quader_api_failure_status(project_session(session_),
                                   std::move(message));
}

QuaderApiVoidResult
QuaderProjectApi::save_to_path(const std::filesystem::path &path,
                               bool update_path) {
  QDR_PROFILE_FUNCTION();
  if (path.empty()) {
    return unsupported("Project save requires a path.");
  }

  ProjectSession &session = project_session(session_);
  QdrProjectWriteResult write = [&]() {
    QDR_PROFILE_SCOPE("Project write QDR document");
    return QdrDocumentWriter::write_project_document_atomic(path,
                                                            session.document());
  }();
  if (!write.ok) {
    return quader_api_failure_status(session, write.message.empty()
                                                  ? "Project save failed."
                                                  : std::move(write.message));
  }

  if (update_path) {
    path_ = path;
  }
  session.mark_clean();
  return project_runtime_status(session, true, "Project saved.");
}

std::optional<std::filesystem::path>
QuaderProjectApi::choose_open_path() const {
  return runtime_hooks_.choose_open_path ? runtime_hooks_.choose_open_path()
                                         : std::nullopt;
}

std::optional<std::filesystem::path>
QuaderProjectApi::choose_save_as_path() const {
  return runtime_hooks_.choose_save_as_path
             ? runtime_hooks_.choose_save_as_path()
             : std::nullopt;
}

} // namespace quader

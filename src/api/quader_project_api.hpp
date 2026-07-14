////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session.hpp"
#include "api/quader_editor_api_types.hpp"

namespace quader {

class ProjectSession;
class QuaderEventsApi;
class QuaderSceneDocumentsApi;
class QuaderProjectApiInternalAccess;

/**
 * Exposes Quader Project API operations through the public Quader API surface.
 */
class QuaderProjectApi final {
public:
    explicit QuaderProjectApi(QuaderApiSession session);
    explicit QuaderProjectApi(ProjectSession &project_session);

    QuaderApiVoidResult create(const std::filesystem::path &root = {});
    QuaderApiVoidResult new_project();
    QuaderApiVoidResult open(const std::filesystem::path& path);
    QuaderApiResult<QuaderObjExportResult>
    export_selected_meshes_as_obj(const std::filesystem::path &path);
    QuaderApiVoidResult close();
    void set_runtime_hooks(QuaderProjectRuntimeHooks hooks);
    void rebind_session(QuaderApiSession session) { session_ = session; }

    [[nodiscard]] bool can_export_selected_meshes_as_obj() const;
    [[nodiscard]] bool has_project() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] const std::filesystem::path &root() const;
    [[nodiscard]] const std::filesystem::path &manifest_path() const;
    [[nodiscard]] std::vector<QuaderRecentProject> recent_projects() const;
    QuaderApiVoidResult remove_recent_project(
        const std::filesystem::path &manifest_path);
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] QuaderProjectMetadata metadata() const;

private:
    void attach_scene_documents(QuaderSceneDocumentsApi &documents);
    void attach_events(QuaderEventsApi &events);
    void set_binding_changed_callback(
        std::function<void(QuaderApiSession)> callback);
    /// Captures whether scene-close preflight may continue and its API status.
    struct SceneClosePreparationResult {
      QuaderApiVoidResult status;
      bool proceed = false;
    };
    [[nodiscard]] SceneClosePreparationResult
    close_open_scenes(std::string_view action_id);
    void remember_open_project();

    friend class QuaderEditorApi;
    friend class QuaderProjectApiInternalAccess;
    [[nodiscard]] QuaderApiVoidResult unsupported(std::string message) const;
    [[nodiscard]] std::optional<std::filesystem::path>
    choose_export_obj_path() const;
    [[nodiscard]] std::optional<QuaderApiVoidResult>
    dirty_replacement_preflight(std::string_view action_id);

    QuaderApiSession session_;
    ProjectSession *project_session_ = nullptr;
    QuaderSceneDocumentsApi *scene_documents_ = nullptr;
    QuaderEventsApi *events_ = nullptr;
    std::function<void(QuaderApiSession)> binding_changed_;
    std::filesystem::path path_;
    std::filesystem::path empty_path_;
    QuaderProjectRuntimeHooks runtime_hooks_;
};

} // namespace quader

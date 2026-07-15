////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/session/scene_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_material_catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace quader {

class StandardMaterialEditSessionService;
class ProjectClipboardService;

/// Owns one heap-stable scene document resident in the project workspace.
struct OpenSceneSession final {
  SceneSessionId session_id;
  SceneId scene_id;
  std::string display_name;
  std::optional<ProjectRelativePath> path;
  std::unique_ptr<SceneSession> session;
  bool identity_dirty = false;

  [[nodiscard]] bool saved() const noexcept { return path.has_value(); }
  [[nodiscard]] bool dirty() const {
    return identity_dirty || (session != nullptr && session->dirty());
  }
};

/// Reports a project or scene-document lifecycle operation outcome.
struct ProjectSessionResult final {
  bool ok = false;
  bool changed = false;
  std::string message;
  std::optional<SceneSessionId> scene_session_id;
  std::vector<std::string> warnings;
};

/**
 * Owns one folder-backed project and its ordered, independently persisted scene
 * sessions. SceneSession pointers remain stable until their tab is closed.
 */
class ProjectSession final {
public:
  ProjectSession();
  ~ProjectSession();

  ProjectSession(const ProjectSession &) = delete;
  ProjectSession &operator=(const ProjectSession &) = delete;
  ProjectSession(ProjectSession &&) = delete;
  ProjectSession &operator=(ProjectSession &&) = delete;

  [[nodiscard]] ProjectSessionResult
  create(const std::filesystem::path &root,
         bool discard_existing_dirty = false);
  [[nodiscard]] ProjectSessionResult
  open(const std::filesystem::path &manifest_path,
       bool discard_existing_dirty = false);
  [[nodiscard]] ProjectSessionResult close(bool discard_dirty = false);

  [[nodiscard]] ProjectSessionResult new_scene();
  [[nodiscard]] ProjectSessionResult open_scene(ProjectRelativePath path);
  [[nodiscard]] ProjectSessionResult activate(SceneSessionId id);
  [[nodiscard]] ProjectSessionResult save(SceneSessionId id);
  [[nodiscard]] ProjectSessionResult save_as(SceneSessionId id,
                                             ProjectRelativePath path);
  [[nodiscard]] ProjectSessionResult save_all();
  [[nodiscard]] ProjectSessionResult close_scene(SceneSessionId id,
                                                 bool discard_dirty = false);
  [[nodiscard]] ProjectSessionResult
  set_active_material(ProjectAssetId material_id);
  [[nodiscard]] ProjectSessionResult install_material_catalog(
      ProjectMaterialCatalog material_catalog,
      std::optional<ProjectAssetId> active_material = std::nullopt);

  void set_resource_root(std::filesystem::path resource_root);

  [[nodiscard]] bool has_project() const noexcept {
    return workspace_.has_value();
  }
  [[nodiscard]] bool has_active_scene() const noexcept {
    return find(active_scene_id_) != nullptr;
  }
  [[nodiscard]] bool has_dirty_scenes() const;
  [[nodiscard]] bool has_dirty_material() const;
  [[nodiscard]] StandardMaterialEditSessionService &standard_material_editor();
  [[nodiscard]] const StandardMaterialEditSessionService &
  standard_material_editor() const;
  [[nodiscard]] ProjectClipboardService &clipboard();
  [[nodiscard]] const ProjectClipboardService &clipboard() const;
  [[nodiscard]] const ProjectWorkspaceStorage *workspace() const noexcept {
    return workspace_ ? &*workspace_ : nullptr;
  }
  [[nodiscard]] const ProjectManifest *manifest() const noexcept {
    return workspace_ ? &workspace_->manifest : nullptr;
  }
  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] const std::filesystem::path &manifest_path() const noexcept;
  [[nodiscard]] const ProjectMaterialCatalog &material_catalog() const noexcept {
    return material_catalog_;
  }
  [[nodiscard]] std::optional<ProjectAssetId>
  active_material_id() const noexcept;
  [[nodiscard]] ProjectIoResult<ProjectMaterialPreview>
  material_preview(ProjectAssetId material_id) const;
  [[nodiscard]] ProjectIoResult<ProjectMaterialTextureData>
  material_texture(ProjectAssetId material_id,
                   StandardMaterialTextureSlot slot) const;
  [[nodiscard]] std::span<const std::unique_ptr<OpenSceneSession>>
  open_scenes() const noexcept {
    return open_scenes_;
  }
  [[nodiscard]] SceneSessionId active_scene_id() const noexcept {
    return active_scene_id_;
  }
  [[nodiscard]] std::uint64_t activation_generation() const noexcept {
    return activation_generation_;
  }
  [[nodiscard]] OpenSceneSession *find(SceneSessionId id);
  [[nodiscard]] const OpenSceneSession *find(SceneSessionId id) const;
  [[nodiscard]] OpenSceneSession *active_scene();
  [[nodiscard]] const OpenSceneSession *active_scene() const;

  /** Inert binding used only while the workspace intentionally has zero tabs. */
  [[nodiscard]] SceneSession &fallback_scene() noexcept {
    return fallback_scene_;
  }

private:
  [[nodiscard]] SceneSessionId allocate_session_id();
  void release_session_id(SceneSessionId id);
  void release_open_scene_ids();
  [[nodiscard]] std::unique_ptr<OpenSceneSession>
  make_scene(SceneId scene_id, std::string display_name,
             std::optional<ProjectRelativePath> path,
             SceneContentDocument document);
  [[nodiscard]] ProjectSessionResult install_workspace(
      ProjectWorkspaceStorage workspace,
      ProjectMaterialCatalog material_catalog,
      std::vector<std::unique_ptr<OpenSceneSession>> scenes,
      std::optional<ProjectRelativePath> requested_active,
      std::vector<std::string> warnings);
  [[nodiscard]] ProjectSessionResult persist_workspace();
  [[nodiscard]] ProjectSessionResult persist_manifest(
      ProjectWorkspaceStorage &workspace, ProjectManifest manifest,
      std::vector<ProjectRelativePath> open_paths,
      std::optional<ProjectRelativePath> active_path);
  [[nodiscard]] ProjectSessionResult persist_prepared_manifest(
      ProjectWorkspaceStorage &workspace, ProjectManifest manifest);
  [[nodiscard]] bool cancel_transient_state(OpenSceneSession &scene);
  [[nodiscard]] bool path_is_open(const ProjectRelativePath &path,
                                  SceneSessionId *id = nullptr) const;
  [[nodiscard]] const ProjectSceneCatalogEntry *
  find_catalog_scene(const ProjectRelativePath &path) const;
  [[nodiscard]] bool scene_id_is_open(const SceneId &scene_id,
                                      const ProjectRelativePath *except_path =
                                          nullptr) const;
  [[nodiscard]] std::string unique_untitled_name() const;

  std::optional<ProjectWorkspaceStorage> workspace_;
  std::vector<std::unique_ptr<OpenSceneSession>> open_scenes_;
  std::vector<std::uint32_t> slot_generations_;
  std::vector<std::uint32_t> free_slots_;
  SceneSessionId active_scene_id_;
  std::uint64_t activation_generation_ = 0u;
  SceneSession fallback_scene_;
  ProjectMaterialCatalog material_catalog_;
  std::unique_ptr<StandardMaterialEditSessionService>
      standard_material_editor_;
  std::unique_ptr<ProjectClipboardService> clipboard_;
  std::filesystem::path resource_root_;
  std::filesystem::path empty_path_;
};

} // namespace quader

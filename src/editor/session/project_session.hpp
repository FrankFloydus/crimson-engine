////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/material/project_material_texture_snapshot.hpp"
#include "editor/session/scene_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_material_catalog.hpp"
#include "io/project/recent_scene_store.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {

class StandardMaterialEditSessionService;
class ProjectClipboardService;
class ProjectAutosaveService;
class ProjectAssetDependencyService;
class ProjectMaterialUsageService;
class ProjectMaterialDeletionService;

/// Owns one heap-stable scene document resident in the project workspace.
struct OpenSceneSession final {
  SceneSessionId session_id;
  SceneId scene_id;
  std::string display_name;
  std::optional<ProjectRelativePath> path;
  std::unique_ptr<SceneSession> session;
  bool identity_dirty = false;
  SceneContentRevisionStamp recent_content_stamp;

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
  explicit ProjectSession(std::filesystem::path recent_scene_storage = {});
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
  /// Stages the requested scene before asking the host to replace its workspace.
  [[nodiscard]] ProjectSessionResult open_scene_project(
      const std::filesystem::path &manifest_path, ProjectRelativePath scene_path,
      const std::function<bool()> &prepare_replacement);
  [[nodiscard]] std::vector<RecentSceneEntry> recent_scenes() const;
  [[nodiscard]] ProjectSessionResult close(bool discard_dirty = false);

  [[nodiscard]] ProjectSessionResult new_scene();
  [[nodiscard]] ProjectSessionResult open_scene(ProjectRelativePath path);
  [[nodiscard]] ProjectSessionResult activate(SceneSessionId id);
  [[nodiscard]] ProjectSessionResult save(SceneSessionId id);
  [[nodiscard]] ProjectSessionResult save_as(SceneSessionId id,
                                             ProjectRelativePath path);
  [[nodiscard]] ProjectSessionResult save_all();
  /// Writes an independent recovery copy without committing previews or marking clean.
  [[nodiscard]] ProjectSessionResult
  write_scene_recovery(SceneSessionId id, const ProjectRelativePath &path) const;
  /// Imports a validated recovery copy as a new, dirty, unsaved scene tab.
  [[nodiscard]] ProjectSessionResult
  recover_scene(const ProjectRelativePath &path, std::string display_name);
  [[nodiscard]] ProjectAutosaveService &autosave() noexcept;
  [[nodiscard]] const ProjectAutosaveService &autosave() const noexcept;
  [[nodiscard]] ProjectAssetDependencyService &dependencies() noexcept;
  [[nodiscard]] const ProjectAssetDependencyService &dependencies() const noexcept;
  [[nodiscard]] ProjectMaterialUsageService &material_usage() noexcept;
  [[nodiscard]] const ProjectMaterialUsageService &material_usage() const noexcept;
  [[nodiscard]] ProjectSessionResult close_scene(SceneSessionId id,
                                                 bool discard_dirty = false);
  [[nodiscard]] ProjectSessionResult install_material_catalog(
      ProjectMaterialCatalog material_catalog);

  void set_resource_root(std::filesystem::path resource_root);
  // Applies the user policy to every resident scene and future scene sessions.
  void set_history_max_steps(std::size_t max_steps);

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
  /// Resolves persistent deletion without exposing project IO to consumers.
  [[nodiscard]] ProjectAssetId
  canonical_material_id(ProjectAssetId material_id) const;
  [[nodiscard]] ProjectIoResult<ProjectMaterialPreview>
  material_preview(ProjectAssetId material_id) const;
  [[nodiscard]] ProjectIoResult<ProjectMaterialTextureData>
  material_texture(ProjectAssetId material_id,
                   StandardMaterialTextureSlot slot) const;
  /// Shares one decoded image per texture identity and installed catalog.
  [[nodiscard]] std::shared_ptr<const ProjectMaterialTextureSnapshot>
  material_texture_image(ProjectAssetId material_id,
                         StandardMaterialTextureSlot slot) const;
  /// Resolves one committed albedo through validated project storage.
  [[nodiscard]] ProjectIoResult<ProjectMaterialAlbedoTextureInfo>
  material_albedo_texture_info(ProjectAssetId material_id) const;
  [[nodiscard]] std::uint64_t material_texture_revision() const noexcept {
    return material_texture_revision_;
  }
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
  friend class ProjectMaterialDeletionService;
  [[nodiscard]] ProjectSessionResult open_workspace(
      const std::filesystem::path &manifest_path, bool discard_existing_dirty,
      std::optional<ProjectRelativePath> required_scene,
      const std::function<bool()> &prepare_replacement);
  void remember_scene(SceneSessionId id) noexcept;
  void remember_edited_scene(SceneSessionId id, bool committed);
  void invalidate_material_textures();
  [[nodiscard]] SceneSessionId allocate_session_id();
  void release_session_id(SceneSessionId id);
  void release_open_scene_ids();
  [[nodiscard]] std::unique_ptr<OpenSceneSession>
  make_scene(SceneId scene_id, std::string display_name,
             std::optional<ProjectRelativePath> path,
             SceneContentDocument document,
             std::optional<ViewportCameraPose> viewport_camera_pose =
                 std::nullopt);
  /** Stages a new tab, persists it, then cancels the outgoing scene before binding. */
  [[nodiscard]] ProjectSessionResult install_scene_and_activate(
      std::unique_ptr<OpenSceneSession> scene, ProjectManifest manifest);
  [[nodiscard]] ProjectSessionResult install_workspace(
      ProjectWorkspaceStorage workspace,
      ProjectMaterialCatalog material_catalog,
      std::vector<std::unique_ptr<OpenSceneSession>> scenes,
      std::optional<ProjectRelativePath> requested_active,
      std::vector<std::string> warnings,
      const std::function<bool()> &prepare_replacement = {},
      std::optional<ProjectManifest> original_manifest = std::nullopt);
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
  std::unordered_set<std::uint32_t> free_slot_membership_;
  SceneSessionId active_scene_id_;
  std::uint64_t activation_generation_ = 0u;
  std::size_t history_max_steps_ = EditorHistoryService::kDefaultMaxSteps;
  std::shared_ptr<PathArraySessionDefaults> path_array_defaults_;
  std::shared_ptr<ExtrudeSessionDefaults> extrude_defaults_;
  std::shared_ptr<ToolSessionDefaults> tool_defaults_;
  SceneSession fallback_scene_;
  ProjectMaterialCatalog material_catalog_;
  std::uint64_t material_texture_revision_ = 0u;
  mutable std::unordered_map<
      ProjectAssetId, std::shared_ptr<const ProjectMaterialTextureSnapshot>,
      ProjectAssetIdHash> material_texture_images_;
  std::unique_ptr<StandardMaterialEditSessionService>
      standard_material_editor_;
  std::unique_ptr<ProjectClipboardService> clipboard_;
  std::unique_ptr<ProjectAutosaveService> autosave_;
  std::unique_ptr<ProjectAssetDependencyService> dependencies_;
  std::unique_ptr<ProjectMaterialUsageService> material_usage_;
  std::filesystem::path resource_root_;
  std::filesystem::path empty_path_;
  RecentSceneStore recent_scene_store_;
  SceneSessionId recent_front_scene_;
  std::optional<ProjectRelativePath> recent_front_path_;
};

} // namespace quader

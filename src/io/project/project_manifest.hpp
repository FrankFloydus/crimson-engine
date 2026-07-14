////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_file_system.hpp"
#include "io/project/project_types.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

inline constexpr std::string_view kQuaderProjectManifestFormat =
    "quader.project";
inline constexpr std::uint32_t kQuaderProjectManifestVersion = 2u;
inline constexpr std::uint32_t kOldestQuaderProjectManifestVersion = 1u;

/// Maps a persistent scene identity to its project-relative file location.
struct ProjectSceneCatalogEntry {
  SceneId id;
  ProjectRelativePath path;

  friend bool operator==(const ProjectSceneCatalogEntry &,
                         const ProjectSceneCatalogEntry &) = default;
};

/// Owns persistent project metadata without embedding scene payloads.
class ProjectManifest final {
public:
  ProjectManifest() = default;

  [[nodiscard]] static ProjectIoResult<ProjectManifest>
  create(ProjectId id, std::string name);

  [[nodiscard]] static ProjectIoResult<ProjectManifest> hydrate(
      ProjectId id, std::string name,
      std::vector<ProjectSceneCatalogEntry> scenes,
      std::vector<ProjectRelativePath> open_scene_paths,
      std::optional<ProjectRelativePath> active_scene_path,
      std::optional<ProjectAssetId> active_material_id = std::nullopt);

  [[nodiscard]] const ProjectId &id() const noexcept { return id_; }
  [[nodiscard]] const std::string &name() const noexcept { return name_; }
  [[nodiscard]] std::span<const ProjectSceneCatalogEntry> scenes() const {
    return scenes_;
  }
  [[nodiscard]] std::span<const ProjectRelativePath>
  open_scene_paths() const {
    return open_scene_paths_;
  }
  [[nodiscard]] const std::optional<ProjectRelativePath> &
  active_scene_path() const noexcept {
    return active_scene_path_;
  }
  [[nodiscard]] const std::optional<ProjectAssetId> &
  active_material_id() const noexcept {
    return active_material_id_;
  }

  [[nodiscard]] const ProjectSceneCatalogEntry *
  find_scene(const SceneId &id) const;
  [[nodiscard]] const ProjectSceneCatalogEntry *
  find_scene(const ProjectRelativePath &path) const;

  [[nodiscard]] ProjectIoResult<void>
  register_scene(ProjectSceneCatalogEntry scene);
  [[nodiscard]] ProjectIoResult<void> unregister_scene(const SceneId &id);

  /**
   * Replaces saved tab order and active scene in one invariant-preserving edit.
   * Every supplied path must already exist in the scene catalog.
   */
  [[nodiscard]] ProjectIoResult<void> set_workspace(
      std::vector<ProjectRelativePath> open_scene_paths,
      std::optional<ProjectRelativePath> active_scene_path);
  [[nodiscard]] ProjectIoResult<void>
  set_active_material(std::optional<ProjectAssetId> material_id);

private:
  ProjectId id_;
  std::string name_;
  std::vector<ProjectSceneCatalogEntry> scenes_;
  std::vector<ProjectRelativePath> open_scene_paths_;
  std::optional<ProjectRelativePath> active_scene_path_;
  std::optional<ProjectAssetId> active_material_id_;
};

/// Bundles a validated project filesystem with its loaded manifest.
struct ProjectWorkspaceStorage {
  ProjectFileSystem file_system;
  ProjectManifest manifest;
  std::filesystem::path manifest_path;
};

/// Creates, reads, validates, and atomically writes `.qproj` manifests.
class ProjectManifestStore final {
public:
  [[nodiscard]] static ProjectIoResult<ProjectWorkspaceStorage>
  create_in_empty_directory(const std::filesystem::path &root);

  [[nodiscard]] static ProjectIoResult<ProjectWorkspaceStorage>
  open(const std::filesystem::path &manifest_path);

  [[nodiscard]] static ProjectIoResult<void>
  save_atomic(const ProjectWorkspaceStorage &workspace);

  [[nodiscard]] static ProjectIoResult<void> save_atomic(
      const ProjectFileSystem &file_system,
      const std::filesystem::path &manifest_path,
      const ProjectManifest &manifest);

  [[nodiscard]] static std::string serialize(const ProjectManifest &manifest);
  [[nodiscard]] static ProjectIoResult<ProjectManifest>
  deserialize(std::string_view text);
};

} // namespace quader
